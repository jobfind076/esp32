#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LED1_PIN   GPIO_NUM_4
#define LED2_PIN   GPIO_NUM_5
#define LED3_PIN   GPIO_NUM_6

#define BUTTON_GPIO           GPIO_NUM_7
#define DEBOUNCE_TIME_US      100000 // 100 мікросекунд (100 мс)

enum TestLeds 
{ 
    LED1 = 0, 
    LED2, 
    LED3, 
};

static const char *TAG = "ISR_DEBOUNCE_EXAMPLE";
static QueueHandle_t gpio_evt_queue = NULL;

// Переменні для збереження часу останнього переривання
static int64_t last_interrupt_time = 0;

// Обробник переривання (ISR)
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    int64_t current_time = esp_timer_get_time(); // Отримуємо точний час в мікросекундах
    uint32_t gpio_num = (uint32_t) arg;

    // Перевіряємо, чи пройшло достатньо часу з моменту останнього спрацьовування
    if ((current_time - last_interrupt_time) > DEBOUNCE_TIME_US) {
        last_interrupt_time = current_time;
        
        // Відправляємо подію в чергу для обробки в основному таску
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

static void init_leds()
{
    // Налаштування GPIO для світлодіода (вихід)
    gpio_reset_pin(LED1_PIN);
    gpio_set_direction(LED1_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED2_PIN);
    gpio_set_direction(LED2_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED3_PIN);
    gpio_set_direction(LED3_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(LED1_PIN, 0xFF);
}

static void process_led(uint32_t led_off, uint32_t led_on)
{
    gpio_set_level(led_off, 0);
    gpio_set_level(led_on, 0xFF);
}

void app_main(void)
{
    init_leds();

    // Конфігурація GPIO для кнопки
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,      // Переривання по спадаючому фронту (натискання до GND)
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,    // Вбудований підтягуючий резистор
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    // Створення черги для передачі подій з ISR до таску
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    
    // Інсталяція сервісу переривань GPIO та додавання обробника
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void*) BUTTON_GPIO);

    ESP_LOGI(TAG, "Систему ініціалізовано. Очікування натискання кнопки...");

    enum TestLeds led_num = LED1;
    uint32_t io_num;
    
    while (1) 
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) 
        {
            // Оскільки брязкіт відсічений в ISR, тут ми маємо 100% чисте натискання
            ESP_LOGI(TAG, "GPIO[%ld] переривання відбулося. Кнопка натиснута!", io_num);

            // виключаємо світлодіод
            switch (led_num)
            {
                case LED1:
                    process_led(LED1_PIN, LED2_PIN);
                    led_num = LED2;
                    break;

                case LED2:
                    process_led(LED2_PIN, LED3_PIN);
                    led_num = LED3;
                    break;

                case LED3:
                    process_led(LED3_PIN, LED1_PIN);
                    led_num = LED1;
                    break;
            }            
        }
    }
}
