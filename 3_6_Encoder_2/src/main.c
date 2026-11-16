#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char* TAG = "ENCODER_RPM";

// Налаштування пінів та енкодера
#define ENCODER_PIN_A GPIO_NUM_4
#define ENCODER_PIN_B GPIO_NUM_5
#define PULSES_PER_REV 400 // Кількість імпульсів на 1 оберт (PPR * 4 для квадратурного режиму)
#define SAMPLE_PERIOD_MS 50 // Період опитування в мс (20 Гц)

// Налаштування фільтра EMA (від 0.0 до 1.0)
// Менше значення = сильніше згладжування, але більша затримка реакції
#define EMA_ALPHA 0.2f

static pcnt_unit_handle_t pcnt_unit = NULL;
static float filtered_rpm = 0.0f;

// Колбек таймера, який виконується кожні SAMPLE_PERIOD_MS
static void rpm_timer_callback(void* arg)
{
    int pulse_count = 0;

    // Зчитуємо накопичені імпульси та обнуляємо лічильник
    ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

    // Обчислюємо миттєвий RPM:
    // (імпульси / імпульси_на_оберт) * (60000 мс / період_вимірювання_мс)
    float raw_rpm = ((float)pulse_count / PULSES_PER_REV) * (60000.0f / SAMPLE_PERIOD_MS);

    // Застосовуємо фільтр EMA
    filtered_rpm = (EMA_ALPHA * raw_rpm) + ((1.0f - EMA_ALPHA) * filtered_rpm);
}

static void init_encoder_pcnt(void)
{
    // 1. Налаштування юніта PCNT
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,
        .low_limit = -32768,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // 2. Апаратний фільтр брязкоту контактів (glitch filter) — ігнорує імпульси коротші за 1 мкс
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // 3. Налаштування каналів для квадратурного декодування (x4)
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_PIN_A,
        .level_gpio_num = ENCODER_PIN_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_PIN_B,
        .level_gpio_num = ENCODER_PIN_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // Налаштування реакції на фронти сигналів
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // 4. Запуск лічильника
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
}

void app_main(void)
{
    init_encoder_pcnt();

    // Створення періодичного таймера для точного розрахунку швидкості
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &rpm_timer_callback,
        .name = "rpm_calc_timer"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, SAMPLE_PERIOD_MS * 1000)); // період у мікросекундах

    // Головний цикл виводу даних
    while (1) {
        ESP_LOGI(TAG, "Filtered RPM: %.2f", filtered_rpm);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
