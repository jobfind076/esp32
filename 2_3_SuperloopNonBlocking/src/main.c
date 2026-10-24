#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define LED1_PIN   GPIO_NUM_4
#define LED2_PIN   GPIO_NUM_5
#define LED3_PIN   GPIO_NUM_6
#define BUTTON_PIN GPIO_NUM_7

typedef struct tagLedVariables
{
    uint64_t led_interval;
    uint64_t last_led_toggle_time;
    int      led_state;
} LedVariables;

void variant1();
void variant2();
void variant3();

void app_main(void)
{
    // Налаштування GPIO для світлодіода (вихід)
    gpio_reset_pin(LED1_PIN);
    gpio_set_direction(LED1_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED2_PIN);
    gpio_set_direction(LED2_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED3_PIN);
    gpio_set_direction(LED3_PIN, GPIO_MODE_OUTPUT);

    // Налаштування GPIO для кнопки (вхід із підтяжкою до живлення)
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_PIN);

    printf("Старт Superloop... \n");

    // Розблокувати потрібний варіант
    //variant1();
    //variant2();
    variant3();

}

void variant1()
{
    static uint8_t changed = 0;

    // НАШ SUPERLOOP (Нескінченний цикл)
    while (1) 
    {
        // МИТТЄВЕ ОПИТУВАННЯ: Читання стану кнопки
        // Оскільки в циклі немає delay(), кнопка перевіряється тисячі разів на секунду
        if (gpio_get_level(BUTTON_PIN) == 0) 
        {
            gpio_set_level(LED1_PIN, 0xFF);
            changed = 1;

            // Простий неблокуючий захист від брязкоту контактів (debouncing)
            // В реальних проектах краще використовувати переривання (IRQs)
        }
        else
        {
            if (changed) 
            {
                gpio_set_level(LED1_PIN, 0);
                changed = 0;
            }            
        }

        // 3. КРИТИЧНО ДЛЯ ESP32: Поступаємося процесорним часом для FreeRTOS
        // vTaskDelay(1) віддає квант часу іншим фоновим задачам (Wi-Fi, Watchdog тощо).
        // Це блокує задачу всього на 1 тік (зазвичай 1-10 мс), що не заважає логіці,
        // але запобігає спрацьовуванню Watchdog Task Reset.
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

void variant2()
{
    int count = 0;
    uint64_t current_time = esp_timer_get_time();
    LedVariables led;

    led.led_state = 0;
    led.last_led_toggle_time = current_time;
    led.led_interval = pdMS_TO_TICKS(5000000);
    
    // НАШ SUPERLOOP (Нескінченний цикл)
    while (1) 
    {
        current_time = esp_timer_get_time();

        // 1. НЕБЛОКУЮЧИЙ ТАЙМЕР: Блимання світлодіодом
        if (current_time - led.last_led_toggle_time >= led.led_interval) 
        {
            led.led_state = !led.led_state;
            gpio_set_level(LED1_PIN, led.led_state);
            led.last_led_toggle_time = current_time; // Оновлюємо час останньої дії

            printf("count: %d \n", ++count);
        }

        // 2. КРИТИЧНО ДЛЯ ESP32: Поступаємося процесорним часом для FreeRTOS
        // vTaskDelay(1) віддає квант часу іншим фоновим задачам (Wi-Fi, Watchdog тощо).
        // Це блокує задачу всього на 1 тік (зазвичай 1-10 мс), що не заважає логіці,
        // але запобігає спрацьовуванню Watchdog Task Reset.
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void variant3()
{
    uint64_t current_time = esp_timer_get_time();

    LedVariables led1;
    LedVariables led2;
    LedVariables led3;

    led1.led_state = 0;
    led1.last_led_toggle_time = current_time;
    led1.led_interval = pdMS_TO_TICKS(2000000);

    led2.led_state = 0;
    led2.last_led_toggle_time = current_time;
    led2.led_interval = pdMS_TO_TICKS(5000000);

    led3.led_state = 0;
    led3.last_led_toggle_time = current_time;
    led3.led_interval = pdMS_TO_TICKS(10000000);

    
    // НАШ SUPERLOOP (Нескінченний цикл)
    while (1) 
    {
        current_time = esp_timer_get_time();

        // 1. НЕБЛОКУЮЧИЙ ТАЙМЕР: Блимання світлодіодом 1
        if (current_time - led1.last_led_toggle_time >= led1.led_interval) 
        {
            led1.led_state = !led1.led_state;
            gpio_set_level(LED1_PIN, led1.led_state);
            led1.last_led_toggle_time = current_time; // Оновлюємо час останньої дії
        }

        // 2. НЕБЛОКУЮЧИЙ ТАЙМЕР: Блимання світлодіодом 2
        if (current_time - led2.last_led_toggle_time >= led2.led_interval) 
        {
            led2.led_state = !led2.led_state;
            gpio_set_level(LED2_PIN, led2.led_state);
            led2.last_led_toggle_time = current_time; // Оновлюємо час останньої дії
        }

        // 3. НЕБЛОКУЮЧИЙ ТАЙМЕР: Блимання світлодіодом 3
        if (current_time - led3.last_led_toggle_time >= led3.led_interval) 
        {
            led3.led_state = !led3.led_state;
            gpio_set_level(LED3_PIN, led3.led_state);
            led3.last_led_toggle_time = current_time; // Оновлюємо час останньої дії
        }

        // 3. КРИТИЧНО ДЛЯ ESP32: Поступаємося процесорним часом для FreeRTOS
        // vTaskDelay(1) віддає квант часу іншим фоновим задачам (Wi-Fi, Watchdog тощо).
        // Це блокує задачу всього на 1 тік (зазвичай 1-10 мс), що не заважає логіці,
        // але запобігає спрацьовуванню Watchdog Task Reset.
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}
