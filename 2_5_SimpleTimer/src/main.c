#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BLINK_GPIO GPIO_NUM_2

static const char *TAG = "SimpleTimer";

static QueueHandle_t gpio_evt_queue = NULL;


// Callback функція, яка викликається під час спрацювання Alarm
static bool IRAM_ATTR example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data) 
{
    // Важливо: всередині ISR не можна використовувати звичайний ESP_LOGI, лише ESP_DRAM_LOG або прапорці
    // Повертаємо true, якщо всередині розблоковується високорівнева задача (Task)
    static bool led_state = false;
    static uint64_t count_value = 0;

    led_state = !led_state;
    gpio_set_level(BLINK_GPIO, led_state);
    
    count_value++;

    // Відправляємо подію в чергу для обробки в основному таску
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &count_value, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) 
    {
        portYIELD_FROM_ISR();
    }

    return false; 
}

void app_main(void) 
{
    // Configure the GPIO pin
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    // Створення черги для передачі подій з ISR до таску
    gpio_evt_queue = xQueueCreate(10, sizeof(uint64_t));

    ESP_LOGI(TAG, "Ініціалізація GPTimer для ESP32-S3");

    // 1. Конфігурація таймера
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Джерело тактування за замовчуванням
        .direction = GPTIMER_COUNT_UP,      // Рахунок вгору
        .resolution_hz = 1000000,           // Частота 1 МГц (1 тік = 1 мікросекунда)
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    // 2. Прив'язка колбеку (обробника події)
    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

    // 3. Конфігурація будильника (Alarm)
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 1000000,             // Спрацювати через 1 000 000 тіків (1 секунда)
        .reload_count = 0,                  // Після перезапуску скидати лічильник в 0
        .flags.auto_reload_on_alarm = true, // Увімкнути автоперезапуск таймера
    };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    // 4. Вмикаємо та запускаємо таймер
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    ESP_LOGI(TAG, "GPTimer запущено успішно!");
    
    uint64_t count_value;

    while (1) 
    {
        if (xQueueReceive(gpio_evt_queue, &count_value, portMAX_DELAY)) 
        {
            printf("timer tick = %" PRIu64 "\n", count_value);
        }

        //vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
