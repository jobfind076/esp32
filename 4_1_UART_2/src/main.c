#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

// Pin Definitions (Mapped PA0 -> GPIO 0, PA1 -> GPIO 1)
#define BUTTON_PIN GPIO_NUM_4
#define LED_PIN GPIO_NUM_5

#define UART_PORT_NUM UART_NUM_1
#define UART_BAUD_RATE 115200
#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_18)
#define BUF_SIZE (1024)

static const char *TAG = "UART_ESP32";

// Initialize Button (Input with Pull-up) and LED (Output)
static void gpio_init(void)
{
    gpio_config_t btn_conf = {.pin_bit_mask = (1ULL << BUTTON_PIN),
                              .mode = GPIO_MODE_INPUT,
                              .pull_up_en = GPIO_PULLUP_ENABLE,
                              .pull_down_en = GPIO_PULLDOWN_DISABLE,
                              .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&btn_conf);

    gpio_config_t led_conf = {.pin_bit_mask = (1ULL << LED_PIN),
                              .mode = GPIO_MODE_OUTPUT,
                              .pull_up_en = GPIO_PULLUP_DISABLE,
                              .pull_down_en = GPIO_PULLDOWN_DISABLE,
                              .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&led_conf);
    gpio_set_level(LED_PIN, 0); // Start with LED OFF
}

void uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// Task 1: Check button press and send "code=1\n"
static void button_tx_task(void *arg)
{
    uint8_t code = 0;
    int last_state = 1; // 1 = unpressed (due to internal pull-up)

    while (1)
    {
        int current_state = gpio_get_level(BUTTON_PIN);

        // Detect falling edge (button pressed to GND)
        if (last_state == 1 && current_state == 0)
        {
            code = 1;
            uart_write_bytes(UART_PORT_NUM, &code, 1);
            ESP_LOGI(TAG, "Button pressed");
            vTaskDelay(pdMS_TO_TICKS(50)); // Simple debounce delay
        }
        
        if (last_state == 0 && current_state == 1)
        {
            code = 0;
            uart_write_bytes(UART_PORT_NUM, &code, 1);
            ESP_LOGI(TAG, "Button released");
            vTaskDelay(pdMS_TO_TICKS(50)); // Simple debounce delay
        }

        last_state = current_state;        
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// Task 2: Read UART and turn LED ON if '1' is received
static void uart_rx_task(void *arg)
{
    uint8_t data[1];

    while (1)
    {
        // Read data from UART with a 100ms timeout
        int len = uart_read_bytes(UART_PORT_NUM, data, (BUF_SIZE - 1), pdMS_TO_TICKS(100));
        if (len > 0)
        {
            // Check if '1' is anywhere in the received payload (handles "1" or "code=1")
            if (data[0] == 1)
            {
                gpio_set_level(LED_PIN, 1);
                ESP_LOGI(TAG, "LED on GPIO %d turned ON", LED_PIN);
            }
            else
            {
                gpio_set_level(LED_PIN, 0);
                ESP_LOGI(TAG, "LED on GPIO %d turned OFF", LED_PIN);
            }
        }
    }
}

void app_main(void)
{
    gpio_init();
    uart_init();

    xTaskCreate(button_tx_task, "button_tx_task", 2048, NULL, 10, NULL);
    xTaskCreate(uart_rx_task, "uart_rx_task", 2048, NULL, 10, NULL);
}
