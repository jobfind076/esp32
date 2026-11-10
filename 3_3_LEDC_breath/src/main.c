#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>

static const char* TAG = "LEDC_BREATH";

// --- Конфігурація ---
#define LED_GPIO GPIO_NUM_4 // Пін LED
#define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE // ESP32-S3 підтримує Low Speed
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT // 13-бітна роздільність (0-8191)
#define LEDC_FREQUENCY 5000 // 5 кГц
#define FADE_TIME_MS 1500 // Час одного переходу (1.5 секунди)

// Черга для обробки подій завершення затухання
static QueueHandle_t ledc_fade_queue = NULL;

// --- Функція зворотного виклику переривання ---
// Увага: Ця функція виконується в контексті ISR, тому не можна викликати блокуючі функції
static bool IRAM_ATTR ledc_fade_done_cb(const ledc_cb_param_t* param, void* user_arg)
{
    BaseType_t high_task_awoken = pdFALSE;
    uint32_t event = 1;
    // Відправляємо подію в чергу, щоб розблокувати задачу
    xQueueSendFromISR(ledc_fade_queue, &event, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting LEDC Breath Example");

    // 1. Створення черги
    ledc_fade_queue = xQueueCreate(10, sizeof(uint32_t));

    // 2. Конфігурація таймера LEDC
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_SPEED_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 3. Конфігурація каналу LEDC
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_SPEED_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_FADE_END, // Увімкнути переривання після завершення затухання
        .gpio_num = LED_GPIO,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // 4. Встановлення функції затухання (обов'язково перед використанням fade)
    ESP_ERROR_CHECK(ledc_fade_func_install(0));

    // 5. Реєстрація функції зворотного виклику
    ledc_cbs_t cbs = {
        .fade_cb = ledc_fade_done_cb
    };
    ESP_ERROR_CHECK(ledc_cb_register(LEDC_SPEED_MODE, LEDC_CHANNEL, &cbs, NULL));

    // Максимальне значення duty для 13 біт
    uint32_t max_duty = (1 << LEDC_DUTY_RES) - 1;
    bool fade_in = true;
    uint32_t event;

    // Початковий запуск: з 0 до максимуму
    ESP_ERROR_CHECK(ledc_set_fade_with_time(LEDC_SPEED_MODE, LEDC_CHANNEL, max_duty, FADE_TIME_MS));
    ESP_ERROR_CHECK(ledc_fade_start(LEDC_SPEED_MODE, LEDC_CHANNEL, LEDC_FADE_NO_WAIT));

    while (1) {
        // Чекаємо на сигнал завершення затухання від переривання
        if (xQueueReceive(ledc_fade_queue, &event, portMAX_DELAY)) {
            // Змінюємо напрямок
            fade_in = !fade_in;

            uint32_t target_duty = fade_in ? max_duty : 0;

            // Запускаємо наступне затухання
            // Використовуємо ledc_set_fade_with_time для контролю тривалості
            ESP_ERROR_CHECK(ledc_set_fade_with_time(LEDC_SPEED_MODE, LEDC_CHANNEL, target_duty, FADE_TIME_MS));
            // LEDC_FADE_NO_WAIT: не блокувати задачу, повернутися одразу
            ESP_ERROR_CHECK(ledc_fade_start(LEDC_SPEED_MODE, LEDC_CHANNEL, LEDC_FADE_NO_WAIT));

            ESP_LOGI(TAG, "Fading %s", fade_in ? "IN (Brighten)" : "OUT (Dim)");
        }
    }
}
