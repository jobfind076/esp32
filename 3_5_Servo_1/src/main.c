#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "POT_SERVO";

// ================== Конфігурація ADC ==================
#define POT_ADC_UNIT ADC_UNIT_1
#define POT_ADC_CHANNEL ADC_CHANNEL_4 // GPIO5 = ADC1_CH4
#define POT_ADC_ATTEN ADC_ATTEN_DB_12 // Діапазон вимірювання до ~2.9V, достатньо для потенціометра 3.3V

// ================== Конфігурація LEDC ==================
#define SERVO_GPIO 4
#define SERVO_LEDC_MODE LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER LEDC_TIMER_0
#define SERVO_LEDC_CH LEDC_CHANNEL_0
#define SERVO_LEDC_RES LEDC_TIMER_14_BIT // 14-бітна роздільна здатність, при 50Hz період 16384 відліків

// Межі імпульсу SG90: 0.5ms ~ 2.5ms, при 50Hz та 14-бітній роздільній здатності
// 0.5ms = 0.5/20 * 16384 ≈ 410
// 2.5ms = 2.5/20 * 16384 ≈ 2048
#define SERVO_MIN_DUTY 410
#define SERVO_MAX_DUTY 2048

static adc_oneshot_unit_handle_t adc_handle;

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = POT_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-бітна, 0~4095
        .atten = POT_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, POT_ADC_CHANNEL, &chan_cfg));
}

static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = SERVO_LEDC_MODE,
        .timer_num = SERVO_LEDC_TIMER,
        .duty_resolution = SERVO_LEDC_RES,
        .freq_hz = 50, // Період 20ms
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CH,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = SERVO_MIN_DUTY, // Початкове положення 0°
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

// Встановлення кута: 0 ~ 180
static void servo_set_angle(float angle)
{
    if (angle < 0)
        angle = 0;
    if (angle > 180)
        angle = 180;

    uint32_t duty = (uint32_t)(SERVO_MIN_DUTY + (angle / 180.0f) * (SERVO_MAX_DUTY - SERVO_MIN_DUTY));
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CH, duty);
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CH);
}

void app_main(void)
{
    adc_init();
    servo_init();

    int raw = 0;
    float angle = 0;

    while (1) {
        // Зчитування сирого значення ADC (0 ~ 4095)
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, POT_ADC_CHANNEL, &raw));

        // Лінійне відображення на кут 0 ~ 180°
        angle = (raw / 4095.0f) * 180.0f;
        servo_set_angle(angle);

        ESP_LOGI(TAG, "ADC raw: %4d, angle: %6.1f°", raw, angle);
        vTaskDelay(pdMS_TO_TICKS(50)); // Оновлення 20 разів на секунду, достатньо плавно
    }
}