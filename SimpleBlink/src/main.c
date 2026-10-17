#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

//  
//  MCU pin GND -> Resistor 330 Om -> LED -> MCU pin 2 
//

#define BLINK_GPIO GPIO_NUM_2

static const char *TAG = "blink";

void app_main(void)
{
    // Configure the GPIO pin
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    bool led_state = false;

    while (1) {
        led_state = !led_state;
        gpio_set_level(BLINK_GPIO, led_state);
        ESP_LOGI(TAG, "LED is %s", led_state ? "ON" : "OFF");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second
    }
}
