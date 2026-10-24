#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gpio_filter.h" // Обов'язковий заголовочний файл
#include "esp_log.h"

#define INPUT_BUTTON_GPIO   GPIO_NUM_4
static const char *TAG = "GlitchFilterExample";

void app_main(void)
{
    // 1. Конфігурація стандартного GPIO як входу
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << INPUT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Вмикаємо підтяжку до живлення
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE     // Наразі без переривань
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "GPIO %d ініціалізовано", INPUT_BUTTON_GPIO);

    // 2. Створення структури конфігурації фільтра завад
    gpio_flex_glitch_filter_config_t filter_config = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT, // Зазвичай це APB clock (~80MHz)
        .pin = INPUT_BUTTON_GPIO,
        .window_width_ns = 500, // Ширина фільтрації в наносекундах.
                                // Будь-які імпульси, коротші за ~500 нс, будуть відфільтровані.
    };

    gpio_glitch_filter_handle_t filter_handle = NULL;

    // 3. Виділення ресурсів під новий гнучкий фільтр
    esp_err_t err = gpio_new_flex_glitch_filter(&filter_config, &filter_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Фільтр успішно створено");

        // 4. Активація фільтра
        gpio_glitch_filter_enable(filter_handle);
        ESP_LOGI(TAG, "Фільтр завад увімкнено для GPIO %d", INPUT_BUTTON_GPIO);
    } else {
        ESP_LOGE(TAG, "Помилка створення фільтра: %s", esp_err_to_name(err));
    }

    // Простий цикл для перевірки стану виводу
    while (1) {
        int pin_value = gpio_get_level(INPUT_BUTTON_GPIO);
        ESP_LOGI(TAG, "Поточний рівень на GPIO %d: %d", INPUT_BUTTON_GPIO, pin_value);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // 5. Якщо фільтр більше не потрібен (наприклад, перед глибоким сном):
    // gpio_glitch_filter_disable(filter_handle);
    // gpio_del_glitch_filter(filter_handle);
}
