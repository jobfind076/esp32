#include "driver/dac_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    // 1. Налаштування конфігурації каналу oneshot
    dac_oneshot_config_t oneshot_cfg = {
        .chan_id = DAC_CHAN_0, // Використання каналу 0 (ESP32 відповідає GPIO25)
    };
    dac_oneshot_handle_t dac_handle;

    // 2. Створення каналу (канал автоматично вмикається)
    ESP_ERROR_CHECK(dac_oneshot_new_channel(&oneshot_cfg, &dac_handle));

    // 3. Періодичний вивід різних рівнів напруги
    uint8_t voltage_level = 0;
    while (1) {
        // Вивід напруги: 0~255 відповідає 0~3.3V (приблизно)
        ESP_ERROR_CHECK(dac_oneshot_output_voltage(dac_handle, voltage_level));
        
        // Кожні 500 мс збільшення рівня на 32
        voltage_level += 16; 
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
