#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char* TAG = "VOL_ENCODER";

// Налаштування пінів та параметрів енкодера
#define ENCODER_GPIO_A 4
#define ENCODER_GPIO_B 5
#define PULSES_PER_REV 80 // 20 тіків енкодера * 4 фронти (квадратурний режим)
#define SAMPLE_PERIOD_MS 20 // Період опитування (50 Гц)

// Параметри EMA фільтра та гучності
#define EMA_ALPHA 0.35f // Коефіцієнт згладжування (0.1 - плавно, 0.8 - швидко)
#define VOL_MIN 0
#define VOL_MAX 100

static pcnt_unit_handle_t pcnt_unit = NULL;

// Ініціалізація апаратного лічильника PCNT
static void init_encoder_pcnt(void)
{
    pcnt_unit_config_t unit_config = {
        .high_limit = 10000,
        .low_limit = -10000,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Апаратний фільтр брязкоту контактів (ігнорує імпульси коротші за 1 мкс)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // Налаштування каналів A і B для квадратурного декодування (x4)
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_GPIO_A,
        .level_gpio_num = ENCODER_GPIO_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_GPIO_B,
        .level_gpio_num = ENCODER_GPIO_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // Логіка підрахунку напрямку обертання
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
}

// Задача обробки енкодера та розрахунку гучності
static void volume_control_task(void* arg)
{
    int pulse_count = 0;
    int remainder = 0;
    float ema_rpm = 0.0f;
    int current_volume = 30; // Початкова гучність 30%

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

        // Зчитуємо накопичені імпульси та обнуляємо лічильник
        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

        // Додаємо залишок від попереднього циклу (оскільки 1 клацання = 4 імпульси)
        int total_pulses = pulse_count + remainder;
        int detents = total_pulses / 4; // Кількість фізичних клацань енкодера
        remainder = total_pulses % 4;

        // 1. Розрахунок миттєвого RPM (абсолютне значення швидкості)
        float instant_rpm = (fabsf((float)pulse_count) / PULSES_PER_REV) * (60000.0f / SAMPLE_PERIOD_MS);

        // 2. Застосування фільтра EMA
        ema_rpm = (EMA_ALPHA * instant_rpm) + ((1.0f - EMA_ALPHA) * ema_rpm);

        // 3. Зміна гучності з урахуванням прискорення (балістики)
        if (detents != 0) {
            int multiplier = 1;

            if (ema_rpm > 60.0f) {
                multiplier = 5; // Дуже швидке обертання
            } else if (ema_rpm > 25.0f) {
                multiplier = 3; // Середнє обертання
            } else if (ema_rpm > 10.0f) {
                multiplier = 2; // Помірне прискорення
            }

            current_volume += (detents * multiplier);

            // Обмеження діапазону 0..100
            if (current_volume > VOL_MAX)
                current_volume = VOL_MAX;
            if (current_volume < VOL_MIN)
                current_volume = VOL_MIN;

            ESP_LOGI(TAG, "Гучність: %d%% | RPM (EMA): %.1f | Множник: x%d", current_volume, ema_rpm, multiplier);

            // Тут викликайте вашу функцію зміни гучності ЦАП / кодека (наприклад, set_dac_volume(current_volume))
        }
    }
}

void app_main(void)
{
    init_encoder_pcnt();
    xTaskCreate(volume_control_task, "vol_task", 4096, NULL, 5, NULL);
}
