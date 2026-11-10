#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <math.h>

static const char* TAG = "LEDC_FREQ";

// --- Конфігурація ---
#define LED_GPIO         GPIO_NUM_4 // Пін LED
#define LEDC_SPEED_MODE  LEDC_LOW_SPEED_MODE // ESP32-S3 підтримує Low Speed
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_FREQUENCY   (20000) // кГц

//uint32_t src_clk_freq = 80 * 1000 * 1000; // 80 МГц для APB
//uint32_t desired_freq = LEDC_TIMER_13_BIT;

void app_main(void)
{
    ESP_LOGI(TAG, "Starting LEDC Freq Example");

    // Конфігурація таймера LEDC
    // Функція поверне максимальну розрядність (наприклад, 11)
    //uint32_t max_duty_res = ledc_find_suitable_duty_resolution(src_clk_freq, desired_freq);
    //if (max_duty_res > LEDC_TIMER_13_BIT) { max_duty_res = LEDC_TIMER_13_BIT; }

    uint32_t max_duty_res = 10;

    uint32_t ledc_duty = pow(2, max_duty_res) / 2;

    ESP_LOGI(TAG, "Info: max_duty_res=%d, ledc_duty=%d", max_duty_res, ledc_duty);

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_SPEED_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = max_duty_res,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Конфігурація каналу LEDC
    // 2. Конфігурація каналу
    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_CHANNEL,
        .duty       = 0, // Початкова шпаруватість
        .gpio_num   = LED_GPIO,
        .speed_mode = LEDC_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // 3. Встановлення та застосування шпаруватості 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL, ledc_duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL));    // Максимальне значення duty для 13 біт

    while (1) 
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
