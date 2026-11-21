#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/pulse_cnt.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "SAFE";

// ================== КОНФІГУРАЦІЯ ==================
#define PIN_LENGTH          4
#define MAX_ATTEMPTS        3
#define CORRECT_PIN         "1234"          // Заданий код

#define ENC_GPIO_A          4
#define ENC_GPIO_B          5
#define BTN_GPIO            6
#define SERVO_GPIO          7
#define BUZZER_GPIO         8

#define PCNT_UNIT           PCNT_UNIT_0
#define PCNT_H_LIM          30000
#define PCNT_L_LIM          (-30000)

// Сервопривід: 0° = замкнено, 90° = відкрито
#define SERVO_LOCKED_DUTY   410             // ~0.5ms при 50Hz, 14-bit
#define SERVO_UNLOCKED_DUTY 1229            // ~1.5ms при 50Hz, 14-bit

#define PULSES_PER_STEP 4
#define DEBOUNCE_TIME_US 200000 // 200 мс антибрязкіт кнопки

// ================== ГЛОБАЛЬНІ ЗМІННІ ==================
static QueueHandle_t btn_queue;
static int current_digit = 0;
static int pin_digits[PIN_LENGTH] = {0};
static int pin_index = 0;
static int last_direction = 0;             // 0 = ще не було обертання, 1 = CW, -1 = CCW
static int attempts = 0;
static bool locked_out = false;

// ================== LEDC / СЕРВОПРИВІД ==================
static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = SERVO_LOCKED_DUTY,
        .hpoint = 0,
    };
    ledc_channel_config(&ch);
}

static void servo_set_duty(uint32_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ================== ЗУМЕР / МЕЛОДІЯ ==================
static void buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch);
}

// Проста мелодія: послідовність {частота, тривалість_мс}, 0 = пауза
static void play_melody(const uint16_t (*notes)[2], int count)
{
    for (int i = 0; i < count; i++) {
        uint16_t freq = notes[i][0];
        uint16_t dur = notes[i][1];
        if (freq == 0) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
        } else {
            ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, freq);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 512); // 50%
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
        }
        vTaskDelay(pdMS_TO_TICKS(dur));
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

static const uint16_t alarm_melody[][2] = {
    {1000, 150}, {0, 50}, {1000, 150}, {0, 50}, {1000, 300}, {0, 100},
    {800, 150}, {0, 50}, {800, 150}, {0, 50}, {800, 400}
};

static const uint16_t success_melody[][2] = {
    {523, 100}, {659, 100}, {784, 100}, {1047, 300}
};

// ================== PCNT (ЕНКОДЕР) ==================
static pcnt_unit_handle_t encoder_init(void)
{
    pcnt_unit_config_t unit_config = {
        .high_limit = 32000,
        .low_limit = -32000,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Апаратний фільтр брязкоту контактів енкодера
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 5000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENC_GPIO_A,
        .level_gpio_num = ENC_GPIO_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENC_GPIO_B,
        .level_gpio_num = ENC_GPIO_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // Налаштування квадратурного декодування (x4)
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    return pcnt_unit;
}

// ================== КНОПКА ==================
static void IRAM_ATTR btn_isr(void *arg)
{
    static int64_t last_press_time = 0;
    int64_t now = esp_timer_get_time();

    if (now - last_press_time > DEBOUNCE_TIME_US) {
        uint32_t gpio_num = (uint32_t)arg;
        xQueueSendFromISR(btn_queue, &gpio_num, NULL);
        last_press_time = now;
    }
}

static void button_init(void)
{
    btn_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_GPIO, btn_isr, NULL);
}

// ================== ЛОГІКА СЕЙФА ==================
static void reset_attempt(void)
{
    pin_index = 0;
    current_digit = 0;
    last_direction = 0;
    memset(pin_digits, 0, sizeof(pin_digits));
    ESP_LOGI(TAG, "Спроба %d/%d. Введіть код:", attempts + 1, MAX_ATTEMPTS);
}

static void confirm_digit_and_advance(void)
{
    if (pin_index < PIN_LENGTH) {
        pin_digits[pin_index] = current_digit;
        pin_index++;
    }
    current_digit = 0;
    last_direction = 0;

    // Вивід набраного коду з \b для перезапису
    printf("\r");
    for (int i = 0; i < pin_index; i++) {
        printf("%d", pin_digits[i]);
    }
    // Очищення залишку рядка
    for (int i = pin_index; i < PIN_LENGTH; i++) {
        printf(" ");
    }
    printf("\b\b\b\b"); // Повернення курсора
    fflush(stdout);
}

static bool check_pin(void)
{
    char entered[PIN_LENGTH + 1];
    for (int i = 0; i < PIN_LENGTH; i++) {
        entered[i] = '0' + pin_digits[i];
    }
    entered[PIN_LENGTH] = '\0';
    return strcmp(entered, CORRECT_PIN) == 0;
}

// ================== ГОЛОВНА ЗАДАЧА ==================
void app_main(void)
{
    servo_init();
    buzzer_init();
    pcnt_unit_handle_t encoder = encoder_init();
    button_init();

    servo_set_duty(SERVO_LOCKED_DUTY);
    vTaskDelay(pdMS_TO_TICKS(500));

    reset_attempt();
    printf("\r"); // Початкове очищення рядка

    int last_steps = 0;

    while (1) {
        // Перевірка кнопки (скидання спроби)
        uint32_t dummy;
        if (xQueueReceive(btn_queue, &dummy, pdMS_TO_TICKS(20))) {
            ESP_LOGW(TAG, "Кнопка натиснута. Скидання.");
            reset_attempt();
            ESP_ERROR_CHECK(pcnt_unit_clear_count(encoder));
        }

        if (locked_out) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 1. Зчитування обертання енкодера
        int pulse_count = 0;
        pcnt_unit_get_count(encoder, &pulse_count);
        int current_steps = pulse_count / PULSES_PER_STEP;
        int delta = current_steps - last_steps;

        if (delta != 0) {
            last_steps = current_steps;

            int dir = (delta > 0) ? 1 : -1;
            int steps = (delta > 0 ? delta : -delta);
            
            // Перше обертання після скидання/підтвердження
            if (last_direction == 0) 
            {
                last_direction = dir;
            } 
            else if (dir != last_direction) 
            {
                // Зміна напрямку = підтвердження попередньої цифри
                confirm_digit_and_advance();                
                last_direction = dir;
                
                if (pin_index >= PIN_LENGTH) 
                {
                    // Всі цифри введено — перевірка
                    printf("\n");
                    if (check_pin()) 
                    {
                        ESP_LOGI(TAG, "КОД ПРАВИЛЬНИЙ! Доступ дозволено.");
                        play_melody(success_melody, sizeof(success_melody)/sizeof(success_melody[0]));
                        servo_set_duty(SERVO_UNLOCKED_DUTY);
                        vTaskDelay(pdMS_TO_TICKS(3000));
                        servo_set_duty(SERVO_LOCKED_DUTY);
                        attempts = 0; // Скидання лічильника після успіху
                        reset_attempt();
                    } 
                    else 
                    {
                        ESP_LOGE(TAG, "НЕПРАВИЛЬНИЙ КОД.");
                        attempts++;
                        if (attempts >= MAX_ATTEMPTS) {
                            ESP_LOGE(TAG, "СПРОБИ ВИЧЕРПАНО. БЛОКУВАННЯ.");
                            play_melody(alarm_melody, sizeof(alarm_melody)/sizeof(alarm_melody[0]));
                            locked_out = true;
                            while (1) 
                            {
                                vTaskDelay(pdMS_TO_TICKS(1000));
                            }
                        }
                        reset_attempt();
                    }

                    last_steps = 0;
                    ESP_ERROR_CHECK(pcnt_unit_clear_count(encoder));
                }
            }
            else
            {
                // Продовження обертання в тому ж напрямку — інкремент цифри
                current_digit += (dir > 0) ? steps : -steps;
                // Циклічне переповнення 0..9
                while (current_digit < 0) current_digit += 10;
                current_digit %= 10;

                // Вивід з \b: перезапис поточної цифри
                printf("\r");
                
                for (int i = 0; i < pin_index; i++) 
                {
                    printf("%d", pin_digits[i]);
                }
                
                printf("%d", current_digit);
                
                for (int i = pin_index + 1; i < PIN_LENGTH; i++) 
                {
                    printf(" ");
                }

                printf("\b\b\b\b");
                fflush(stdout);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}