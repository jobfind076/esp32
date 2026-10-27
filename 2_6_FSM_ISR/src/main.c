#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <esp_task_wdt.h>

#define BUTTON_PIN GPIO_NUM_7
#define BUTTON_THRESHOLD 2000

// Перелік подій кнопки
typedef enum {
    BUTTON_SHORT_PRESS,
    BUTTON_LONG_PRESS
} button_type_t;

typedef struct tagButtonEvt
{
    button_type_t type;
    uint16_t count;
} ButtonEvt;

static const char *TAG = "FSM_ISR_Timer";
static QueueHandle_t button_evt_queue = NULL;

// Функція зворотного виклику (callback), яка викликається кожні 10 мс
static void periodic_timer_callback(void* arg)
{
    // ВАЖЛИВО: код тут має бути швидким і не блокувати потік (без vTaskDelay)
    ButtonEvt evt;
    evt.type = BUTTON_LONG_PRESS;
    static uint16_t count_value = 0;
    static bool button_pressed = false;

    if (gpio_get_level(BUTTON_PIN) == 0) 
    {
        button_pressed = true;
        count_value++;
    }
    else    
    {
        if (!button_pressed) return;

        if (count_value < BUTTON_THRESHOLD)
        {
            evt.type = BUTTON_SHORT_PRESS;
        }

        evt.count = count_value;
        count_value = 0;
        button_pressed = false;

        // Відправляємо подію в чергу для обробки в основному таску
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(button_evt_queue, &evt, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) 
        {
            portYIELD_FROM_ISR();
        }
    }
}

void app_main(void) 
{
    esp_task_wdt_deinit();

    // Налаштування GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Кнопка притиснута до VCC, при натисканні замикає на GND
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE    // Переривання вимкнені, використовуємо таймер
    };
    gpio_config(&io_conf);

    // Створення черги для передачі подій з ISR до таску
    button_evt_queue = xQueueCreate(10, sizeof(ButtonEvt));

    if (button_evt_queue == NULL) {
        ESP_LOGE(TAG, "Помилка створення черги!");
        return;
    }    

    ESP_LOGI(TAG, "Ініціалізація періодичного таймера...");

    // 1. Конфігурація таймера
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &periodic_timer_callback, // Вказуємо нашу функцію
        .name = "periodic_10ms_timer"         // Ім'я таймера для дебагу
    };

    esp_timer_handle_t periodic_timer;
    // Створюємо таймер
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));

    // 2. Запуск таймера з періодом в мікросекундах (10 мс = 10000 мкс)
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, pdMS_TO_TICKS(10)));
    ESP_LOGI(TAG, "Таймер успішно запущено кожні 10 мс.");

       
    ButtonEvt evt;

    while (1) 
    {
        // Чекаємо на подію з черги (блокуючий виклик, не споживає CPU)
        if (xQueueReceive(button_evt_queue, &evt, portMAX_DELAY)) 
        {
            if (evt.type == BUTTON_SHORT_PRESS) 
            {
                ESP_LOGW(TAG, "[SHORT] Кнопку НАТИСНУТО!, count=%d", evt.count);
            } 
            else if (evt.type == BUTTON_LONG_PRESS) 
            {
                ESP_LOGW(TAG, "[LONG] Кнопку НАТИСНУТО!, count=%d", evt.count);
            }
        }

        //vTaskDelay(pdMS_TO_TICKS(100));
    }
}
