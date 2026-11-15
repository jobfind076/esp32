#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"
#include "soc/system_reg.h"
#include "soc/ledc_reg.h"
#include "soc/ledc_struct.h"
#include <stdio.h>

static const char* TAG = "SG90_REG";

#define SERVO_GPIO 4 // Пін керування (підходить будь-який вільний GPIO)
#define LEDC_TIMER_IDX 0 // Таймер 0 (0..3)
#define LEDC_CH_IDX 0 // Канал 0 (0..7)
#define PWM_RES_BITS 14 // 14-бітна роздільна здатність (0..16383)
#define PWM_MAX_DUTY ((1 << PWM_RES_BITS) - 1)
#define PWM_PERIOD_US 20000 // Період 20 мс (50 Гц)

// Розрахунок дільника для 50 Гц при APB_CLK = 80 МГц та 14 бітах:
// div = 80_000_000 / (50 * 16384) = 97.65625
// У форматі з фіксованою комою (10 цілих біт + 8 дробових біт): 97.65625 * 256 = 25000
#define CLK_DIV_VAL 25000

// Структура для калібрування конкретного екземпляра SG90
typedef struct {
    uint32_t min_us; // Імпульс для 0° (типово 500–600 мкс)
    uint32_t max_us; // Імпульс для 180° (типово 2400–2500 мкс)
    int16_t trim_us; // Програмне зміщення нуля (в мкс)
} servo_cal_t;

static servo_cal_t sg90_cal = {
    .min_us = 540,
    .max_us = 2420,
    .trim_us = 0
};

/**
 * @brief Ініціалізація LEDC напряму через регістри ESP32-S3
 */
static void ledc_direct_init(void)
{
    // 1. Вмикаємо тактування та знімаємо скидання з модуля LEDC
    REG_SET_BIT(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_LEDC_CLK_EN);
    REG_CLR_BIT(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_LEDC_RST);

    // 2. Обираємо джерело тактування APB_CLK (значення 1 у полі LEDC_APB_CLK_SEL)
    LEDC.conf.apb_clk_sel = 1;

    // 3. Налаштовуємо Таймер 0 на 50 Гц та 14 біт
    LEDC.timer_group[0].timer[0].conf.duty_resolution = PWM_RES_BITS;
    LEDC.timer_group[0].timer[0].conf.clock_divider = CLK_DIV_VAL;
    LEDC.timer_group[0].timer[0].conf.pause = 0;
    LEDC.timer_group[0].timer[0].conf.rst = 0;

    // Обов'язкове застосування параметрів для Low-Speed таймера ESP32-S3
    LEDC.timer_group[0].timer[0].conf.low_speed_update = 1;

    // 4. Налаштовуємо Канал 0 та прив'язуємо його до Таймера 0
    LEDC.channel_group[0].channel[0].conf0.timer_sel = 0;
    LEDC.channel_group[0].channel[0].conf0.idle_lv = 0;
    LEDC.channel_group[0].channel[0].conf0.sig_out_en = 1;

    // Точка перемикання у високий рівень (початок періоду = 0)
    LEDC.channel_group[0].channel[0].hpoint.hpoint = 0;

    // 5. Підключаємо вихід Каналу 0 LEDC до фізичного GPIO через GPIO Matrix
    gpio_reset_pin(SERVO_GPIO);
    gpio_set_direction(SERVO_GPIO, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(SERVO_GPIO, LEDC_LS_SIG_OUT0_IDX, false, false);
}

/**
 * @brief Встановлення ширини імпульсу в мікросекундах через регістри
 */
void servo_write_us(uint32_t pulse_us)
{
    // Обмеження безпеки для механіки SG90
    if (pulse_us < 450)
        pulse_us = 450;
    if (pulse_us > 2550)
        pulse_us = 2550;

    // Перерахунок мкс у значення регістра duty (зсув вліво на 4 біти — вимога формату регістра LEDC)
    uint32_t duty_ticks = (pulse_us * PWM_MAX_DUTY) / PWM_PERIOD_US;

    // У регістрі LEDC_CHn_DUTY_REG молодші 4 біти — дробова частина для апаратного фейдингу
    LEDC.channel_group[0].channel[LEDC_CH_IDX].duty.duty = (duty_ticks << 4);

    // Атомарне оновлення параметрів каналу на початку наступного циклу ШІМ
    LEDC.channel_group[0].channel[LEDC_CH_IDX].conf1.duty_inc = 1;
    LEDC.channel_group[0].channel[LEDC_CH_IDX].conf1.duty_num = 1;
    LEDC.channel_group[0].channel[LEDC_CH_IDX].conf1.duty_cycle = 1;
    LEDC.channel_group[0].channel[LEDC_CH_IDX].conf1.duty_scale = 0;
    LEDC.channel_group[0].channel[LEDC_CH_IDX].conf1.duty_start = 1;
    LEDC.channel_group[0].channel[LEDC_CH_IDX].conf0.low_speed_update = 1;
}

/**
 * @brief Встановлення кута (0.0° .. 180.0°) з урахуванням калібрування
 */
void servo_write_angle(float angle, const servo_cal_t* cal)
{
    if (angle < 0.0f)
        angle = 0.0f;
    if (angle > 180.0f)
        angle = 180.0f;

    float span = (float)(cal->max_us - cal->min_us);
    int32_t target_us = (int32_t)(cal->min_us + (angle / 180.0f) * span) + cal->trim_us;

    servo_write_us((uint32_t)target_us);
}

/**
 * @brief Процедура інтерактивного або крокового калібрування меж SG90
 */
void servo_calibrate_sweep(void)
{
    ESP_LOGI(TAG, "Старт перевірки меж калібрування...");

    // Перевірка центру (90°)
    servo_write_angle(90.0f, &sg90_cal);
    ESP_LOGI(TAG, "Позиція 90° (центр). Перевірте перпендикулярність качалки.");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Перевірка мінімуму (0°)
    servo_write_angle(0.0f, &sg90_cal);
    ESP_LOGI(TAG, "Позиція 0° (%lu мкс). Якщо чути гудіння мотора — збільшіть min_us!", sg90_cal.min_us);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Перевірка максимуму (180°)
    servo_write_angle(180.0f, &sg90_cal);
    ESP_LOGI(TAG, "Позиція 180° (%lu мкс). Якщо є упор — зменшіть max_us!", sg90_cal.max_us);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void app_main(void)
{
    ledc_direct_init();
    ESP_LOGI(TAG, "LEDC ініціалізовано напряму через регістри на GPIO %d", SERVO_GPIO);

    servo_calibrate_sweep();

    while (1) {
        for (int angle = 0; angle <= 180; angle += 30) {
            servo_write_angle((float)angle, &sg90_cal);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        for (int angle = 180; angle >= 0; angle -= 30) {
            servo_write_angle((float)angle, &sg90_cal);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}
