#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "bme280.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "BME280_OLED";

// Налаштування I2C
#define I2C_PORT_NUM I2C_NUM_0
#define I2C_SDA_PIN GPIO_NUM_8
#define I2C_SCL_PIN GPIO_NUM_9
#define I2C_FREQ_HZ 400000
#define I2C_MASTER_TIMEOUT_MS 1000

#define SSD1306_ADDR 0x3C
#define BME280_ADDR 0x76

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t bme280_handle;
static ssd1306_handle_t disp_handle;

static void i2c_master_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_NUM,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));
    ESP_LOGI(TAG, "I2C bus initialized");
}

static esp_err_t bme280_dev_init(i2c_master_bus_handle_t bus_handle)
{
    // Додавання BME280 на шину
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME280_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &bme280_handle));

    // 3. Перевірка ID чіпа (0x60 для BME280, 0x58 для BMP280)
    uint8_t chip_id = 0;
    ESP_ERROR_CHECK(bme280_read_regs(bme280_handle, REG_ID, &chip_id, 1));
    ESP_LOGI(TAG, "Chip ID: 0x%02X", chip_id);

    // 4. Калібрування та конфігурація вимірювань (Normal mode, oversampling x1)
    bme280_read_calibration(bme280_handle);
    bme280_write_reg(bme280_handle, REG_CTRL_HUM, 0x01);  // Вологість x1
    bme280_write_reg(bme280_handle, REG_CONFIG, 0xA0);    // Standby 1000ms, filter off
    bme280_write_reg(bme280_handle, REG_CTRL_MEAS, 0x27); // Temp x1, Press x1, Normal mode
    
    return ESP_OK;
}

static void ssd1306_oled_init()
{
    ssd1306_config_t disp_cfg = {
        .bus = SSD1306_I2C, // SSD1306_I2C or SSD1306_SPI
        .width = 128,
        .height = 64,
        .iface.i2c =
            {
                .port = I2C_PORT_NUM,
                .addr = SSD1306_ADDR,
                .rst_gpio = -1, // Якщо на модулі немає піна RST, залиште -1
            },
    };

    esp_err_t ret = ssd1306_new_i2c(&disp_cfg, &disp_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SSD1306 init failed: %s", esp_err_to_name(ret));
        return;
    }
    ssd1306_clear(disp_handle);
    ssd1306_display(disp_handle);
    ESP_LOGI(TAG, "SSD1306 initialized");
}

void app_main(void)
{
    // Ініціалізація шини I2C (новий драйвер i2c_master)
    i2c_master_init();

    // Налаштування датчика BME280
    bme280_dev_init(bus_handle);

    // Ініціалізація SSD1306
    ssd1306_oled_init();

    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;

    char line_temp[32];
    char line_hum[32];
    char line_pres[32];

    uint8_t raw_data[8];

    while (1)
    {
        ssd1306_clear(disp_handle);
        ssd1306_draw_text(disp_handle, 0, 0, "ESP32-S3 BME280", true);
        
        esp_err_t res = bme280_read_regs(bme280_handle, REG_PRESS_MSB, raw_data, 8);

        if (res == ESP_OK)
        {
            int32_t adc_P = (raw_data[0] << 12) | (raw_data[1] << 4) | (raw_data[2] >> 4);
            int32_t adc_T = (raw_data[3] << 12) | (raw_data[4] << 4) | (raw_data[5] >> 4);
            int32_t adc_H = (raw_data[6] << 8)  | raw_data[7];

            temperature = compensate_temp(adc_T);
            pressure = compensate_press(adc_P);
            humidity = compensate_hum(adc_H);

            float pressure_hpa = pressure / 100.0f;
            float pressure_mmhg = pressure * 0.750062f;

            ESP_LOGI("test", "Read Values: temp = %.2f *C, pres = %.1f hPa, pres = %.2f mmHg, hum = %.2f\%", 
                temperature, pressure_hpa, pressure_mmhg, humidity);

            snprintf(line_temp, sizeof(line_temp), "Temp: %.2f C", temperature);
            snprintf(line_hum, sizeof(line_hum), "Hum:  %.2f %%", humidity);
            snprintf(line_pres, sizeof(line_pres), "Pres: %.0f mmHg", pressure_mmhg);

            ssd1306_draw_text(disp_handle, 0, 16, line_temp, true);
            ssd1306_draw_text(disp_handle, 0, 32, line_hum, true);
            ssd1306_draw_text(disp_handle, 0, 48, line_pres, true);
        }
        else
        {
            ESP_LOGW(TAG, "Не вдалося зчитати дані з BME280");
            ssd1306_draw_text(disp_handle, 0, 0, "BME280 Error!", true);
        }
            
        // Оновлення екрану
        ssd1306_display(disp_handle);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}