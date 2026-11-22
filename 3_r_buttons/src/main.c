#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char* TAG = "ADC_BUTTONS";

// Налаштування ADC (наприклад, GPIO4 = ADC1_CHANNEL_3 на ESP32-S3)
#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_3
#define ADC_ATTEN ADC_ATTEN_DB_12 // Дозволяє вимірювати до ~3.1-3.3V

// Параметри антибрязкоту
#define POLL_INTERVAL_MS 15 // Період опитування
#define DEBOUNCE_COUNT 3 // Кількість однакових зчитувань (3 * 15мс = 45мс)

typedef enum {
    BTN_NONE = 0,
    BTN_1,
    BTN_2,
    BTN_3,
    BTN_4
} button_id_t;

// Функція перетворення сирого значення АЦП (0..4095) у номер кнопки.
// Пороги (thresholds) краще відкалібрувати під ваші реальні резистори!
static button_id_t get_button_from_adc(int raw_val)
{
    if (raw_val < 300) {
        return BTN_1; // ~0V
    } else if (raw_val >= 500 && raw_val < 1100) {
        return BTN_2; // ~0.6V
    } else if (raw_val >= 1250 && raw_val < 1850) {
        return BTN_3; // ~1.17V
    } else if (raw_val >= 2000 && raw_val < 2700) {
        return BTN_4; // ~1.82V
    }
    return BTN_NONE; // > 3000 (~3.3V) або перехідний процес
}

void adc_button_task(void* arg)
{
    // 1. Ініціалізація ADC
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12 біт (0 - 4095)
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config));

    // Змінні для стану та debounce
    button_id_t last_raw_btn = BTN_NONE;
    button_id_t stable_btn = BTN_NONE;
    uint8_t debounce_counter = 0;

    while (1) {
        int adc_raw = 0;
        // Зчитуємо кілька разів для усереднення шумів АЦП
        for (int i = 0; i < 4; i++) {
            int temp = 0;
            adc_oneshot_read(adc1_handle, ADC_CHANNEL, &temp);
            adc_raw += temp;
        }
        adc_raw /= 4;

        ESP_LOGI(TAG, "ADC raw: %d", adc_raw);

        button_id_t current_btn = get_button_from_adc(adc_raw);

        // Логіка програмного debounce
        if (current_btn == last_raw_btn) {
            if (debounce_counter < DEBOUNCE_COUNT) {
                debounce_counter++;
            } else if (stable_btn != current_btn) {
                // Стан стабілізувався і змінився
                stable_btn = current_btn;

                if (stable_btn != BTN_NONE) {
                    ESP_LOGI(TAG, "Натиснуто кнопку: %d", stable_btn);
                    // Тут можна відправити подію в чергу (xQueueSend)
                } else {
                    ESP_LOGI(TAG, "Кнопку відпущено");
                }
            }
        } else {
            // Значення змінилося (брязкіт або нове натискання) — скидаємо лічильник
            debounce_counter = 0;
        }

        last_raw_btn = current_btn;
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    xTaskCreate(adc_button_task, "adc_btn_task", 2048, NULL, 5, NULL);
}
