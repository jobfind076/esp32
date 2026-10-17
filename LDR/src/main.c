#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#define ADC_UNIT           ADC_UNIT_1
#define ADC_CHANNEL        ADC_CHANNEL_3 // ADC1_CH3 is GPIO 4 on the ESP32-S3
#define ADC_ATTEN          ADC_ATTEN_DB_12 // Configures for full 0-3.3V range

static const char *TAG = "LDR_READ";

void app_main(void)
{
    // 1. Configure ADC Oneshot Unit
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 2. Configure ADC Channel Attenuation and Bit Width
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit width for ESP32-S3
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config));

    while (1) {
        int raw_val = 0;
        // Read raw data from the ADC channel
        esp_err_t r = adc_oneshot_read(adc1_handle, ADC_CHANNEL, &raw_val);
        
        if (r == ESP_OK) {
            ESP_LOGI(TAG, "Raw LDR Reading: %d", raw_val);
            
            if (raw_val > 3000) {
                ESP_LOGI(TAG, "Environment Status: Lite");
            } else {
                ESP_LOGI(TAG, "Environment Status: Dark");
            }
        } else {
            ESP_LOGE(TAG, "Failed to read ADC value");
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Read every 1 second
    }

    // Clean up handle if loop ever breaks
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
}
