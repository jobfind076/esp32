#include "ds18b20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "onewire_bus.h"
#include "onewire_device.h"
#include "driver/gpio.h"
#include <stdio.h>

static const char* TAG = "DS18B20_EXAMPLE";

// ================== Налаштування апаратного забезпечення ==================
#define ONEWIRE_GPIO GPIO_NUM_4 // DQ підключено до GPIO4
#define MAX_DS18B20_DEVICES 4   // Максимальна кількість датчиків на шині

void app_main(void)
{
    // ================== 1. Ініціалізація шини 1-Wire ==================
    onewire_bus_handle_t bus = NULL;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = ONEWIRE_GPIO,
    };
    // Використання внутрішнього підтягування (зовнішній резистор 4.7K все одно рекомендується)
    bus_config.flags.en_pull_up = true;

    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10, // Достатньо для читання ROM (8 байт) + команд
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));
    ESP_LOGI(TAG, "Шину 1-Wire ініціалізовано на GPIO %d", ONEWIRE_GPIO);

    // ================== 2. Пошук усіх DS18B20 на шині ==================
    ds18b20_device_handle_t ds18b20s[MAX_DS18B20_DEVICES];
    int device_count = 0;

    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t search_result;

    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    ESP_LOGI(TAG, "Початок пошуку пристроїв...");

    do {
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (search_result == ESP_OK) {
            // Перевірка, чи є пристрій DS18B20
            ds18b20_config_t ds_cfg = {};
            if (ds18b20_new_device_from_enumeration(&next_onewire_device, &ds_cfg, &ds18b20s[device_count]) == ESP_OK) {
                ESP_LOGI(TAG, "Знайдено DS18B20[%d], адреса: %016llX",
                    device_count, next_onewire_device.address);
                device_count++;
            } else {
                ESP_LOGW(TAG, "Знайдено невідомий пристрій, адреса: %016llX",
                    next_onewire_device.address);
            }
        }
    } while (search_result != ESP_ERR_NOT_FOUND && device_count < MAX_DS18B20_DEVICES);

    ESP_ERROR_CHECK(onewire_del_device_iter(iter));

    if (device_count == 0) {
        ESP_LOGE(TAG, "DS18B20 не знайдено! Перевірте підключення та резистор 4.7K.");
        return;
    }

    ESP_LOGI(TAG, "Знайдено %d DS18B20, початок циклічного вимірювання...", device_count);

    // ================== 3. Циклічне зчитування температури ==================
    while (1) {
        // trigger temperature conversion for all sensors on the bus
        ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion_for_all(bus));

        for (int i = 0; i < device_count; i++) {
            float temperature;
            esp_err_t err = ds18b20_get_temperature(ds18b20s[i], &temperature);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "DS18B20[%d] температура: %.2f °C", i, temperature);
            } else {
                ESP_LOGE(TAG, "DS18B20[%d] помилка читання: %s", i, esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Інтервал вимірювання 1 секунда
    }
}
