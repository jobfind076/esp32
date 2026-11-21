#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

// Налаштування пінів
#define ENC_PIN_A GPIO_NUM_4
#define ENC_PIN_B GPIO_NUM_5
#define ENC_PIN_BTN GPIO_NUM_6

// Налаштування енкодера (4 імпульси на 1 клацання для стандартного KY-040)
#define PULSES_PER_STEP 4
#define DEBOUNCE_TIME_US 200000 // 200 мс антибрязкіт кнопки

typedef enum {
    STATE_OP1, // Введення першого числа
    STATE_OPERATOR, // Вибір операції (+, -, *, /)
    STATE_OP2, // Введення другого числа
    STATE_RESULT // Відображення результату
} calc_state_t;

static QueueHandle_t btn_evt_queue = NULL;
static int last_line_len = 0;

// Обробник переривання кнопки
static void IRAM_ATTR btn_isr_handler(void* arg)
{
    static int64_t last_press_time = 0;
    int64_t now = esp_timer_get_time();

    if (now - last_press_time > DEBOUNCE_TIME_US) {
        uint32_t gpio_num = (uint32_t)arg;
        xQueueSendFromISR(btn_evt_queue, &gpio_num, NULL);
        last_press_time = now;
    }
}

// Ініціалізація кнопки
static void init_button(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Спрацювання при натисканні (спад фронту)
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << ENC_PIN_BTN),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    btn_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENC_PIN_BTN, btn_isr_handler, (void*)ENC_PIN_BTN);
}

// Ініціалізація апаратного PCNT для квадратурного енкодера
static pcnt_unit_handle_t init_encoder(void)
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
        .edge_gpio_num = ENC_PIN_A,
        .level_gpio_num = ENC_PIN_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENC_PIN_B,
        .level_gpio_num = ENC_PIN_A,
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

// Функція оновлення рядка в консолі з використанням \b
static void update_console_line(const char* new_str)
{
    // Повертаємо курсор назад на довжину попереднього рядка за допомогою \b
    for (int i = 0; i < last_line_len; i++) {
        putchar('\b');
    }
    // Затираємо старі символи пробілами та знову повертаємо курсор
    for (int i = 0; i < last_line_len; i++) {
        putchar(' ');
    }
    for (int i = 0; i < last_line_len; i++) {
        putchar('\b');
    }

    // Виводимо новий рядок
    printf("%s", new_str);
    fflush(stdout); // Обов'язково скидаємо буфер UART/USB
    last_line_len = strlen(new_str);
}

void app_main(void)
{
    // Вимикаємо буферизацію stdout для миттєвого виводу символів
    setvbuf(stdout, NULL, _IONBF, 0);

    init_button();
    pcnt_unit_handle_t encoder = init_encoder();

    const char ops[] = { '+', '-', '*', '/' };
    calc_state_t state = STATE_OP1;

    int op1 = 0;
    int op2 = 0;
    int op_idx = 0;
    int last_steps = 0;
    char buf[64];

    printf("Калькулятор готовий. Обертайте енкодер:\n");
    snprintf(buf, sizeof(buf), "%d", op1);
    update_console_line(buf);

    while (1) {
        // 1. Зчитування обертання енкодера
        int pulse_count = 0;
        pcnt_unit_get_count(encoder, &pulse_count);
        int current_steps = pulse_count / PULSES_PER_STEP;
        int delta = current_steps - last_steps;

        if (delta != 0) {
            last_steps = current_steps;

            if (state == STATE_OP1) {
                op1 += delta;
                snprintf(buf, sizeof(buf), "%d", op1);
                update_console_line(buf);
            } else if (state == STATE_OPERATOR) {
                // Циклічний вибір знаку операції (+, -, *, /)
                op_idx = (op_idx + delta) % 4;
                if (op_idx < 0)
                    op_idx += 4;
                snprintf(buf, sizeof(buf), "%d %c ", op1, ops[op_idx]);
                update_console_line(buf);
            } else if (state == STATE_OP2) {
                op2 += delta;
                snprintf(buf, sizeof(buf), "%d %c %d", op1, ops[op_idx], op2);
                update_console_line(buf);
            }
        }

        // 2. Перевірка натискання кнопки
        uint32_t io_num;
        if (xQueueReceive(btn_evt_queue, &io_num, pdMS_TO_TICKS(20))) {
            switch (state) {
            case STATE_OP1:
                state = STATE_OPERATOR;
                snprintf(buf, sizeof(buf), "%d %c ", op1, ops[op_idx]);
                update_console_line(buf);
                break;

            case STATE_OPERATOR:
                state = STATE_OP2;
                op2 = 0;
                snprintf(buf, sizeof(buf), "%d %c %d", op1, ops[op_idx], op2);
                update_console_line(buf);
                break;

            case STATE_OP2:
                state = STATE_RESULT;
                char current_op = ops[op_idx];
                if (current_op == '/' && op2 == 0) {
                    snprintf(buf, sizeof(buf), "%d / 0 = Error (Div by 0)", op1);
                } else if (current_op == '+') {
                    snprintf(buf, sizeof(buf), "%d + %d = %d", op1, op2, op1 + op2);
                } else if (current_op == '-') {
                    snprintf(buf, sizeof(buf), "%d - %d = %d", op1, op2, op1 - op2);
                } else if (current_op == '*') {
                    snprintf(buf, sizeof(buf), "%d * %d = %d", op1, op2, op1 * op2);
                } else if (current_op == '/') {
                    // Виводимо з точністю до 2 знаків після коми або ціле, якщо ділиться націло
                    if (op1 % op2 == 0) {
                        snprintf(buf, sizeof(buf), "%d / %d = %d", op1, op2, op1 / op2);
                    } else {
                        snprintf(buf, sizeof(buf), "%d / %d = %.2f", op1, op2, (float)op1 / op2);
                    }
                }
                update_console_line(buf);
                printf("\n"); // Перехід на новий рядок після завершення обчислення
                last_line_len = 0;
                break;

            case STATE_RESULT:
                // Скидання для нового обчислення
                op1 = 0;
                op2 = 0;
                op_idx = 0;
                state = STATE_OP1;
                snprintf(buf, sizeof(buf), "%d", op1);
                update_console_line(buf);
                break;
            }
        }
    }
}
