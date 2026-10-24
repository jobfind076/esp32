#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LED1_PIN    GPIO_NUM_4
#define LED2_PIN    GPIO_NUM_5
#define LED3_PIN    GPIO_NUM_6
#define BUTTON1_PIN GPIO_NUM_7
#define BUTTON2_PIN GPIO_NUM_8

#define DEBOUNCE_DELAY_US   500000 // 50 мс у мікросекундах

#define DELAY_IN_200_MS   2000000
#define DELAY_IN_500_MS   5000000
#define DELAY_IN_1000_MS 10000000

static const char *TAG = "super_nb2";

typedef struct tagLedVariables
{
    uint8_t  pin;
    uint64_t interval;
    uint64_t last_toggle_time;
    int      state;
} LedVariables;

typedef struct tagButton
{
    int pressed_count;
    uint64_t last_toggle_time;
    void (*click)(int, LedVariables*, int);
} Button;

void but1_init()
{
    // Конфігурація GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Спад сигналу
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON1_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1 // Вбудована підтяжка
    };
    gpio_config(&io_conf);    
}

void btn1_click(int i_pin, LedVariables* led, int count)
{
    if (led == NULL)
    {
        ESP_LOGI(TAG, "btn1 click param 'led' is NULL");    
        return;
    }

    ESP_LOGI(TAG, "btn1 click, count=%d", count);

    // interval change: 200 ms, 500 ms, 1 sec
    switch (led->interval)
    {
        case DELAY_IN_200_MS:
            led->interval = DELAY_IN_500_MS;
            return;

        case DELAY_IN_500_MS:
            led->interval = DELAY_IN_1000_MS;
            return;

        case DELAY_IN_1000_MS:
            led->interval = DELAY_IN_200_MS;
            return;           
    }
}

void but2_init()
{
    // Конфігурація GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Спад сигналу
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON2_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1 // Вбудована підтяжка
    };
    gpio_config(&io_conf);    
}

void btn2_click(int i_pin, LedVariables* led, int count)
{
    if (led == NULL)
    {
        ESP_LOGI(TAG, "btn2 click param 'led' is NULL");    
        return;
    }

    ESP_LOGI(TAG, "btn2 click, count=%d", count);

    if (led->state) 
    {
        gpio_set_level(led->pin, 0);
        led->state = 0;
    }            
    else
    {
        gpio_set_level(led->pin, 0xFF);
        led->state = 1;
    }
}

void app_main(void)
{
    // Налаштування GPIO для світлодіода (вихід)
    gpio_reset_pin(LED1_PIN);
    gpio_set_direction(LED1_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED2_PIN);
    gpio_set_direction(LED2_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED3_PIN);
    gpio_set_direction(LED3_PIN, GPIO_MODE_OUTPUT);

    but1_init();
    but2_init();

    uint64_t current_time = esp_timer_get_time();

    // Налаштування структур LED
    LedVariables led1;
    LedVariables led2;
    LedVariables led3;

    led1.pin = LED1_PIN;
    led1.state = 0;

    led2.pin = LED2_PIN;
    led2.state = 0;
    led2.last_toggle_time = current_time;
    led2.interval = DELAY_IN_500_MS;

    led3.pin = LED3_PIN;
    led3.state = 0;
    led3.last_toggle_time = current_time;
    led3.interval = DELAY_IN_1000_MS;

    Button btn1;
    btn1.pressed_count = 0;
    btn1.last_toggle_time = current_time;
    btn1.click = &btn1_click;

    Button btn2;
    btn2.pressed_count = 0;
    btn2.last_toggle_time = current_time;
    btn2.click = &btn2_click;

    volatile bool paused = false;

    printf("Старт Superloop...\n");

    // НАШ SUPERLOOP (Нескінченний цикл)
    while (1) 
    {
        current_time = esp_timer_get_time();

        // МИТТЄВЕ ОПИТУВАННЯ: Читання стану кнопки btn1
        // Оскільки в циклі немає delay(), кнопка перевіряється тисячі разів на секунду
        if (gpio_get_level(BUTTON2_PIN) == 0) 
        {
            // Простий неблокуючий захист від брязкоту контактів (debouncing)
            // В реальних проектах краще використовувати переривання (IRQs)
            if (current_time - btn2.last_toggle_time >= DEBOUNCE_DELAY_US)
            {
                btn2.pressed_count++;
                btn2.last_toggle_time = current_time;
                paused = (paused ? false : true);
                btn2.click(BUTTON1_PIN, &led1, btn2.pressed_count);
            }
        }

        if (!paused)
        {
            // МИТТЄВЕ ОПИТУВАННЯ: Читання стану кнопки btn2
            // Оскільки в циклі немає delay(), кнопка перевіряється тисячі разів на секунду
            if (gpio_get_level(BUTTON1_PIN) == 0) 
            {
                // Простий неблокуючий захист від брязкоту контактів (debouncing)
                // В реальних проектах краще використовувати переривання (IRQs)
                if (current_time - btn1.last_toggle_time >= DEBOUNCE_DELAY_US)
                {
                    //ESP_LOGI(TAG, "btn2 pressed");
                    btn1.click(BUTTON1_PIN, &led2, btn1.pressed_count);
                    btn1.pressed_count++;
                    btn1.last_toggle_time = current_time;
                }
            }        

            // НЕБЛОКУЮЧИЙ ТАЙМЕР: Блимання світлодіодом 2
            if (current_time - led2.last_toggle_time >= pdMS_TO_TICKS(led2.interval)) 
            {
                led2.state = !led2.state;
                gpio_set_level(LED2_PIN, led2.state);
                led2.last_toggle_time = current_time; // Оновлюємо час останньої дії
            }

            // НЕБЛОКУЮЧИЙ ТАЙМЕР: Блимання світлодіодом 3
            if (current_time - led3.last_toggle_time >= pdMS_TO_TICKS(led3.interval)) 
            {
                led3.state = !led3.state;
                gpio_set_level(LED3_PIN, led3.state);
                led3.last_toggle_time = current_time; // Оновлюємо час останньої дії
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
