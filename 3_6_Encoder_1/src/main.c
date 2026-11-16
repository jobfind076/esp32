#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"

#define ENCODER_GPIO_A 4
#define ENCODER_GPIO_B 5
#define ENCODER_HIGH_LIMIT 10000
#define ENCODER_LOW_LIMIT -10000

static const char* TAG = "PCNT_X4";

pcnt_unit_handle_t init_encoder_pcnt_x4(void)
{
    // 1. Налаштування та створення PCNT юніта
    pcnt_unit_config_t unit_config = {
        .high_limit = ENCODER_HIGH_LIMIT,
        .low_limit = ENCODER_LOW_LIMIT,
        .flags.accum_count = true, // Дозволяє накопичувати значення при переповненні лімітів        
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // 2. Налаштування апаратного фільтра брязкоту (Glitch Filter)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000, // Ігнорувати імпульси коротші за 1 мкс (1000 нс)
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // 3. Налаштування Канала 0 (Імпульси на A, контроль рівня по B)
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_GPIO_A,
        .level_gpio_num = ENCODER_GPIO_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    // 4. Налаштування Канала 1 (Імпульси на B, контроль рівня по A)
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_GPIO_B,
        .level_gpio_num = ENCODER_GPIO_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // 5. Логіка підрахунку фронтів для режиму X4
    // Для Канала A:
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, // На передньому фронті (Rising)
        PCNT_CHANNEL_EDGE_ACTION_INCREASE // На задньому фронті (Falling)
        ));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, // Якщо B == 1, залишаємо дію як є
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE // Якщо B == 0, інвертуємо дію
        ));

    // Для Канала B (дзеркальна логіка):
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, // На передньому фронті (Rising)
        PCNT_CHANNEL_EDGE_ACTION_DECREASE // На задньому фронті (Falling)
        ));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, // Якщо A == 1, залишаємо дію як є
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE // Якщо A == 0, інвертуємо дію
        ));

    // 6. Додавання точок спостереження (обов'язково, якщо увімкнено accum_count)
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, ENCODER_HIGH_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, ENCODER_LOW_LIMIT));

    // 7. Увімкнення, очищення та запуск лічильника
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    ESP_LOGI(TAG, "PCNT X4 енкодер успішно ініціалізовано");
    return pcnt_unit;
}

void app_main(void)
{
    pcnt_unit_handle_t pcnt_unit = init_encoder_pcnt_x4();
    int pulse_count = 0;

    while (1) 
    {
        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
        ESP_LOGI(TAG, "pulse_count=%d", pulse_count);
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}