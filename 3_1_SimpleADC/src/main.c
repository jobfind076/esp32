#include <stdio.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ADC_ONESHOT";

#define EXAMPLE_ADC1_CHAN3 ADC_CHANNEL_3 // Пример: GPIO на ADC1 (зависит от разводки платы ESP32-S3)
#define EXAMPLE_ADC_ATTEN  ADC_ATTEN_DB_12 // Аттенюация 12 дБ (диапазон измерения до ~3.3В)

void app_main(void)
{
    // 1. Инициализация ADC-модуля (oneshot)
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 2. Конфигурация канала (разрядность и затухание/attenuation)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Обычно 12 бит (0 - 4095)
        .atten = EXAMPLE_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN3, &config));

    int adc_raw = 0;

    while (1) {
        // 3. Чтение сырого (raw) значения из ADC
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN3, &adc_raw));
        ESP_LOGI(TAG, "ADC1 Channel[%d] Raw Value: %d", EXAMPLE_ADC1_CHAN3, adc_raw);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 4. Очистка ресурсов (при необходимости выхода из задачи)
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
}
