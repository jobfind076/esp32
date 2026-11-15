#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"
#include "soc/ledc_reg.h"
#include "soc/ledc_struct.h"
#include "soc/system_reg.h"
#include <stdint.h>

#define SERVO_GPIO 4
#define TIMER_RES_BITS 14
#define MAX_DUTY (1 << TIMER_RES_BITS) // 16384

// Розрахунок дільника для 50 Гц при APB_CLK = 80 МГц та 14 бітах:
// div = 80_000_000 / (50 * 16384) = 97.65625
// У форматі з фіксованою комою (10 цілих біт + 8 дробових біт): 97.65625 * 256 = 25000
#define CLK_DIV_VAL 25000

static void ledc_direct_init(void)
{
    // 1. Вмикаємо тактування та знімаємо скидання з модуля LEDC
    REG_SET_BIT(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_LEDC_CLK_EN);
    REG_CLR_BIT(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_LEDC_RST);

    // 2. Обираємо джерело тактування APB_CLK (значення 1 у полі LEDC_APB_CLK_SEL)
    LEDC.conf.apb_clk_sel = 1;

    // 3. Налаштовуємо Таймер 0 на 50 Гц та 14 біт
    LEDC.timer_group[0].timer[0].conf.duty_resolution = TIMER_RES_BITS;
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

static void servo_set_angle_direct(uint8_t angle)
{
    if (angle > 180) {
        angle = 180;
    }

    // Перетворюємо кут (0..180°) у тривалість імпульсу (500..2500 мкс)
    uint32_t pulse_us = 500 + ((uint32_t)angle * 2000) / 180;

    // Розраховуємо значення заповнення для періоду 20000 мкс (50 Гц)
    uint32_t duty = (pulse_us * MAX_DUTY) / 20000;

    // Поле duty має 4 молодші дробові біти, тому зсуваємо ціле значення вліво на 4
    LEDC.channel_group[0].channel[0].duty.duty = (duty << 4);

    // Налаштовуємо миттєве оновлення без апаратного згасання (fade)
    LEDC.channel_group[0].channel[0].conf1.duty_inc = 1;
    LEDC.channel_group[0].channel[0].conf1.duty_num = 1;
    LEDC.channel_group[0].channel[0].conf1.duty_cycle = 1;
    LEDC.channel_group[0].channel[0].conf1.duty_scale = 0;

    // Запускаємо оновлення та фіксуємо зміни в регістрах каналу
    LEDC.channel_group[0].channel[0].conf1.duty_start = 1;
    LEDC.channel_group[0].channel[0].conf0.low_speed_update = 1;
}

void app_main(void)
{
    ledc_direct_init();

    while (1) {
        servo_set_angle_direct(0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        servo_set_angle_direct(90);
        vTaskDelay(pdMS_TO_TICKS(1000));

        servo_set_angle_direct(180);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
