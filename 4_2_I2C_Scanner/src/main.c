#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

// Налаштування I2C
#define I2C_PORT_NUM I2C_NUM_0
#define I2C_SDA_PIN GPIO_NUM_8
#define I2C_SCL_PIN GPIO_NUM_9

static const char *TAG = "i2c_scanner";

void app_main(void)
{
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    ESP_LOGI(TAG, "Scanning I2C bus...");
    int found_devices = 0;

    for (uint8_t addr = 0; addr < 128; addr++)
    {
        esp_err_t ret = i2c_master_probe(bus_handle, addr, 50);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Found device at address: 0x%02X", addr);
            found_devices++;
        }
    }

    if (found_devices == 0)
    {
        ESP_LOGW(TAG, "No I2C devices found.");
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
