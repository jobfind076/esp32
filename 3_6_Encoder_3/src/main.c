#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define ENCODER_GPIO_A 4
#define LED_GPIO 6
#define PULSES_PER_REV 20
#define EMA_ALPHA 0.2f
#define SAMPLE_PERIOD_MS 100 // Період вимірювання в мс

static const char* TAG = "LED_RPM_EMA_ENC";

void app_main(void)
{
    // 1. Налаштування світлодіода
    gpio_config_t led_io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_io_conf);

    // 2. Налаштування апаратного лічильника імпульсів (PCNT) в ESP-IDF v5+
    pcnt_unit_handle_t pcnt_unit = NULL;
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,
        .low_limit = -32767,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = ENCODER_GPIO_A,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    // Рахуємо по наростаючому фронту (Positive edge)
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));

    // Фільтр від дребезгу
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    int total_pulses_acc = 0;
    float filtered_rpm = 0.0f;
    int led_state = 0;

    int prev_pulse_count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

        int current_pulse_count = 0;
        pcnt_unit_get_count(pcnt_unit, &current_pulse_count);

        // Різниця імпульсів за інтервал SAMPLE_PERIOD_MS
        int delta_pulses = current_pulse_count - prev_pulse_count;
        prev_pulse_count = current_pulse_count;

        total_pulses_acc += delta_pulses;

        // Перевірка на кожний повний оберт (20 імпульсів)
        if (total_pulses_acc >= PULSES_PER_REV) 
        {
            if ((total_pulses_acc % PULSES_PER_REV) == 0)
            {
                led_state = !led_state;
                gpio_set_level(LED_GPIO, led_state); // вмикаємо/перемикаємо світлодіод на кожний повний оберт
            }
        }

        // Розрахунок RPM за період:
        // RPM = (delta_pulses / PULSES_PER_REV) * (60000 / SAMPLE_PERIOD_MS)
        float instant_rpm = ((float)delta_pulses / (float)PULSES_PER_REV) * (60000.0f / (float)SAMPLE_PERIOD_MS);

        // Застосування фільтра EMA: RPM_filtered = alpha * RPM_instant + (1 - alpha) * RPM_filtered_prev
        filtered_rpm = EMA_ALPHA * instant_rpm + (1.0f - EMA_ALPHA) * filtered_rpm;

        ESP_LOGI(TAG, "Total pulses: %d | Delta Pulses: %d | Instant RPM: %.1f | EMA RPM: %.1f",
            total_pulses_acc, delta_pulses, instant_rpm, filtered_rpm);
    }
}
