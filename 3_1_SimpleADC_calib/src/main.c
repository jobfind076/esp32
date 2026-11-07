#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char* TAG = "DARK_SENSOR";

// Конфігурація GPIO та ADC
#define BLINK_LED_GPIO GPIO_NUM_5 // Вбудований або зовнішній світлодіод
#define ADC_CHANNEL ADC_CHANNEL_3 // GPIO4 на ESP32-S3 (ADC1)
#define ADC_ATTEN ADC_ATTEN_DB_12 // Повний діапазон напруги (до ~3.3V)

// Налаштування фільтрації та гістерезису
#define OVERSAMPLING_SAMPLES 16 // Кількість замірів для оверсемплінгу
#define SAMPLE_PERIOD_MS 500 // Період замірів (неблокуючий)

#define THRESHOLD_DARK_MV 1900 // Нижній поріг: напруга впала нижче -> ТЕМРЯВА (вмикаємо LED)
#define THRESHOLD_LIGHT_MV 2800 // Верхній поріг: напруга зросла вище -> СВІТЛО (вимикаємо LED)

static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t* out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip ADC calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}

void app_main(void)
{
    // 1. Конфігурація GPIO для індикатора (LED)
    gpio_reset_pin(BLINK_LED_GPIO);
    gpio_set_direction(BLINK_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_LED_GPIO, 1);

    // 2. Инициализация ADC-модуля (oneshot)
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 3. Конфігурація каналу ADC
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Обычно 12 бит (0 - 4095)
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config));

    adc_cali_handle_t adc_cali_handle = NULL;
    bool do_calibration = example_adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL, ADC_ATTEN, &adc_cali_handle);

    // Змінні стану
    bool is_dark = false;

    ESP_LOGI(TAG, "Dark sensor initialized. Loop started...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

        uint32_t raw_sum = 0;
        int raw_value = 0;

        // Оверсемплінг (16 швидких замірів поспіль)
        for (int i = 0; i < OVERSAMPLING_SAMPLES; i++) {
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL, &raw_value));
            raw_sum += raw_value;
        }
        uint32_t adc_raw_avg = raw_sum / OVERSAMPLING_SAMPLES;

        // Переведення в мілівольти (mV) за допомогою калібрування
        int voltage_mv = 0;
        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw_avg, &voltage_mv));
        } 
        else 
        {
            // Фолбек, якщо калібрування відсутнє (наближене значення для 12-bit при 3.3V)
            voltage_mv = (adc_raw_avg * 3300) / 4095;
        }

        // Логіка двопорогового гістерезису (Захист від мерехтіння)
        
        if (is_dark) {
            // Зараз стан "ТЕМРЯВА". Щоб вийти з нього, світла має стати достатньо (вище верхнього порогу)
            if (voltage_mv > THRESHOLD_LIGHT_MV) {
                is_dark = false;
                gpio_set_level(BLINK_LED_GPIO, 0); // Вимикаємо сигналізатор
                ESP_LOGI(TAG, "--> LIGHT DETECTED (%d mV)", voltage_mv);
            }
        } else {
            // Зараз стан "СВІТЛО". Щоб увійти в темряву, напруга має впасти нижче нижнього порогу
            if (voltage_mv < THRESHOLD_DARK_MV) {
                is_dark = true;
                gpio_set_level(BLINK_LED_GPIO, 1); // Вмикаємо сигналізатор (LED)
                ESP_LOGW(TAG, "--> DARK DETECTED (%d mV)", voltage_mv);
            }
        }
        
        // Періодичний вивід поточних значень для дебагу
        ESP_LOGI(TAG, "Raw Avg: %lu, Voltage: %d mV, State: %s", adc_raw_avg, voltage_mv, is_dark ? "DARK" : "LIGHT");
    }

    // Очищення ресурсів (у разі виходу з нескінченного циклу)
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));

    if (do_calibration) {
        example_adc_calibration_deinit(adc_cali_handle);
    }
}
