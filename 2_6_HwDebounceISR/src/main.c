#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LED_PIN               GPIO_NUM_2
#define BUTTON_GPIO           GPIO_NUM_7

static const char *TAG = "HW_DEBOUNCE";
static QueueHandle_t gpio_evt_queue = NULL;

// Обробник переривання (ISR)
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    //int64_t current_time = esp_timer_get_time(); // Отримуємо точний час в мікросекундах
    uint32_t gpio_num = (uint32_t) arg;

    // Відправляємо подію в чергу для обробки в основному таску
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) 
    {
        portYIELD_FROM_ISR();
    }
}

void app_main(void)
{
    // Налаштування GPIO для світлодіода (вихід)
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

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

    bool led_state = false;
    uint32_t io_num;
    uint32_t count = 0;
    
    while (1) 
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) 
        {
            // Оскільки брязкіт відсічений в ISR, тут ми маємо 100% чисте натискання
            ESP_LOGI(TAG, "N=[%d] GPIO[%d] переривання відбулося. Кнопка натиснута!", ++count, io_num);            

            gpio_set_level(LED_PIN, led_state);
            led_state = !led_state;
        }
    }
}
