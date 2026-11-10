#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

// Визначення пінів та параметрів ШІМ
#define SERVO_MIN_PULSEWIDTH_US 500 // Мінімальна ширина імпульсу (0 градусів) у мкс
#define SERVO_MAX_PULSEWIDTH_US 2500 // Максимальна ширина імпульсу (180 градусів) у мкс
#define SERVO_MAX_DEGREE 180 // Максимальний кут повороту

#define SERVO_PULSE_GPIO (4) // Номер GPIO, до якого підключено жовтий дріт

// Налаштування таймера LEDC
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_14_BIT // 14-бітна роздільна здатність
#define LEDC_FREQUENCY (50) // Частота 50 Гц для сервоприводів

// Функція для розрахунку duty cycle на основі кута
static uint32_t angle_to_duty(int angle)
{
    if (angle < 0)
        angle = 0;
    if (angle > SERVO_MAX_DEGREE)
        angle = SERVO_MAX_DEGREE;

    // Перераховуємо кут у мікросекунди імпульсу
    uint32_t pulse_us = SERVO_MIN_PULSEWIDTH_US + (((SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) * angle) / SERVO_MAX_DEGREE);

    // Перераховуємо мікросекунди у значення duty для 14-бітного таймера при 50 Гц (період 20000 мкс)
    // Формула: (pulse_us / 20000) * (2^14 - 1)
    uint32_t duty = (pulse_us * ((1 << 14) - 1)) / 20000;
    return duty;
}

void init_servo(void)
{
    // 1. Конфігурація таймера LEDC
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Конфігурація каналу LEDC
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = SERVO_PULSE_GPIO,
        .duty = angle_to_duty(0), // Стартова позиція 0 градусів
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void app_main(void)
{
    // Ініціалізація серво
    init_servo();

    printf("Servo SG90 initialized successfully!\n");

    while (1) {
        // Поворот на 0 градусів
        printf("Setting angle to 0°\n");
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(0)));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Поворот на 90 градусів
        printf("Setting angle to 90°\n");
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(90)));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Поворот на 180 градусів
        printf("Setting angle to 180°\n");
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(180)));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
