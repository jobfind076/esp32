#include "driver/i2c_master.h"
#include "ds1307.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "RTC_OLED";

// Налаштування I2C
#define I2C_PORT_NUM I2C_NUM_0
#define I2C_SDA_PIN GPIO_NUM_8
#define I2C_SCL_PIN GPIO_NUM_9
#define I2C_FREQ_HZ 100000

// Адреси пристроїв
#define DS1307_ADDR    0x68
#define DS_EEPROM_ADDR 0x50
#define SSD1306_ADDR   0x3C


void app_main(void)
{
    // 1. Ініціалізація шини I2C (новий драйвер i2c_master)
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_NUM,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));
    ESP_LOGI(TAG, "I2C bus initialized");

    for (int i = 0; i < 128; i++)
    {
        esp_err_t ret = i2c_master_probe(bus_handle, i, 50);
        if (ret == ESP_OK)
        {
            if (i == DS1307_ADDRESS)
            {
                ESP_LOGI(TAG, "found ds1307 address: 0x%02X", i);
            }
            else if (i == 0x50)
            {
                ESP_LOGI(TAG, "found eeprom address: 0x%02X", i);
            }
            else
            {
                ESP_LOGI(TAG, "found i2c device address: 0x%02X", i);
            }
        }
    }

    // 2. Ініціалізація DS1307
    ds1307_config_t ds_cfg = {
        .ds1307_device.scl_speed_hz = I2C_FREQ_HZ,
        .ds1307_device.device_address = DS1307_ADDR,
    };
    ds1307_handle_t ds_handle;
    esp_err_t ret = ds1307_init(bus_handle, &ds_cfg, &ds_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "DS1307 init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "DS1307 initialized");

    {
        // За потреби тут можна встановити початковий час:
        //struct tm t = {.tm_year = 126, .tm_mon = 10, .tm_mday = 24, .tm_hour = 12, .tm_min = 39, .tm_sec = 30};
        //ds1307_set_datetime(ds_handle, &t);
    }

    // 3. Ініціалізація SSD1306
    ssd1306_config_t disp_cfg = {
        .bus = SSD1306_I2C,
        .width = 128,
        .height = 64,
        .iface.i2c =
            {
                .port = I2C_PORT_NUM,
                .addr = SSD1306_ADDR,
                .rst_gpio = -1, // Якщо на модулі немає піна RST, залиште -1
            },
    };
    ssd1306_handle_t disp_handle;
    ret = ssd1306_new_i2c(&disp_cfg, &disp_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SSD1306 init failed: %s", esp_err_to_name(ret));
        return;
    }
    ssd1306_clear(disp_handle);
    ssd1306_display(disp_handle);
    ESP_LOGI(TAG, "SSD1306 initialized");

    // 4. Основний цикл: читання часу та відображення
    struct tm now;
    char time_str[32];
    char date_str[32];

    while (1)
    {
        // Читання часу з RTC
        ret = ds1307_get_datetime(ds_handle, &now);
        if (ret == ESP_OK)
        {
            // Форматування рядків
            // У struct tm рік рахується від 1900, місяць від 0
            snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
            snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);

            // Очищення та вивід на дисплей
            ssd1306_clear(disp_handle);

            // Виводимо дату (верхній рядок) та час (нижній, більшим шрифтом якщо підтримується)
            ssd1306_draw_text(disp_handle, 0, 0, date_str, true);
            ssd1306_draw_text(disp_handle, 0, 16, time_str, true);

            // Оновлення екрану
            ssd1306_display(disp_handle);

            ESP_LOGI(TAG, "Time: %s %s", date_str, time_str);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read time from DS1307");
            ssd1306_clear(disp_handle);
            ssd1306_draw_text(disp_handle, 0, 0, "RTC Error", true);
            ssd1306_display(disp_handle);
        }

        // Оновлення раз на секунду
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
