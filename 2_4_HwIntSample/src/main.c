#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO GPIO_NUM_4 // Використовуйте безпечні GPIO

// Обробник переривання, розміщений в IRAM
static void IRAM_ATTR gpio_isr_handler(void* arg) 
{
    uint32_t gpio_num = (uint32_t) arg;
    // Логіка має бути максимально швидкою (наприклад, інкремент лічильника або робота з чергою FreeRTOS)
}

void app_main(void) 
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,        // Спрацьовування по спадному фронту
        .mode = GPIO_MODE_INPUT,               // Режим GPIO
        .pin_bit_mask = (1ULL << BUTTON_GPIO), // Маска піна
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE       // Підтяжка до живлення
    };
    gpio_config(&io_conf);

    // Встановлення глобальної служби обробки переривань GPIO
    gpio_install_isr_service(0);
    
    // Прив'язка конкретного обробника до нашого піна
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void*) BUTTON_GPIO);
}
