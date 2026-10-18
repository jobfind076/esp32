#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define BUTTON_GPIO GPIO_NUM_4
#define PWM_GPIO GPIO_NUM_5

#define ESP_INTR_FLAG_DEFAULT   0

#define LEDC_OUTPUT_IO          PWM_GPIO            // GPIO пін для виходу ШІМ
#define LEDC_MODE               LEDC_LOW_SPEED_MODE // ESP32-S3 підтримує тільки Low Speed
#define LEDC_CHANNEL            LEDC_CHANNEL_0      // Використовуємо канал 0
#define LEDC_TIMER              LEDC_TIMER_0        // Використовуємо таймер 0
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT    // Роздільна здатність 8 біт (макс. 255)
#define LEDC_FREQUENCY          (5000)              // Частота ШІМ в Гц (5 кГц)


static const char *TAG = "pwm";

// Обробник переривання (ISR) - виконується максимально швидко
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    static uint8_t duty = 0;

    // Перевіряємо, чи кнопка все ще натиснута
    if (gpio_get_level(gpio_num) == 0) 
    { 
        if (duty == 255) 
        { 
            duty = 0; 
        }
        else 
        {
            duty += 64;
        }

        // Встановлюємо нове значення duty cycle
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
        // Оновлюємо значення на фізичному піні
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

        //ESP_LOGI(TAG, "button GPIO %ld pressed!", gpio_num);
    }
}

void app_main(void)
{
    // Configure the GPIO pin
    // 1. Конфігурація GPIO для кнопки
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO), // Вибираємо потрібний пін
        .mode = GPIO_MODE_INPUT,               // Режим: Вхід
        .pull_up_en = GPIO_PULLUP_ENABLE,      // Вмикаємо внутрішню підтяжку до 3.3V
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Вимикаємо стяжку до GND
        .intr_type = GPIO_INTR_NEGEDGE         // Спрацьовувати при падінні сигналу (натисканні)
    };
    gpio_config(&io_conf);

    // 1. Конфігурація GPIO для транзистора з LED
    gpio_reset_pin(PWM_GPIO);
    gpio_set_direction(PWM_GPIO, GPIO_MODE_OUTPUT);

    // 2. Ініціалізація та встановлення сервісу переривань GPIO
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    
    // 3. Додаємо обробник для конкретного піна кнопки
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void*) BUTTON_GPIO);

    // 4. Конфігурація LEDC Таймера
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 5. Конфігурація LEDC Каналу
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = 0, // Початковий duty cycle
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_LOGI(TAG, "Систему ініціалізовано. Очікування натискання...");    

    while (1) {
        //ESP_LOGI(TAG, "tick");
        vTaskDelay(pdMS_TO_TICKS(30)); // Wait for 1 second
    }
}
