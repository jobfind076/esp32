#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define UART_PORT_NUM UART_NUM_1
#define UART_BAUD_RATE 115200
#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_18)
#define BUF_SIZE (1024)

static const char *TAG = "UART_ESP32";

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

void app_main(void)
{
    uart_init();
    //uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    const char *msg = "Hello STM32!\n";

    while (1)
    {
        // Відправка даних
        uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
        ESP_LOGI(TAG, "Sent: %s", msg);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
