#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/dac_continuous.h"
#include "driver/dac_cosine.h"
#include "esp_err.h"


// Визначення конфігурації
#define DAC_GPIO          25           // ESP32 DAC канал 0 відповідає GPIO25
#define SAMPLE_RATE       100000       // Частота дискретизації DMA 100 кГц
#define WAVE_POINTS       100          // Кількість точок вибірки на один період пилкоподібного сигналу
// Розрахункова частота пилкоподібного сигналу = 100000 / 100 = 1000 Гц

void sample_saw()
{
    // Налаштування конфігурації безперервного режиму DAC
    dac_continuous_config_t cont_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0, // Використання каналу 0 (GPIO25)
        .desc_num  = 4,                    // Кількість дескрипторів DMA
        .buf_size  = 256,                  // Розмір кожного буфера DMA (байт)
        .freq_hz   = SAMPLE_RATE,          // Частота перетворення DAC
        .offset    = 0,
        .clk_src   = DAC_DIGI_CLK_SRC_DEFAULT,
    };
    dac_continuous_handle_t dac_handle;
    ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &dac_handle));

    // Увімкнення безперервного режиму DAC
    ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));

    // Підготовка буфера даних пилкоподібного сигналу (8-бітне розділення, 0~255)
    uint8_t sawtooth_buf[WAVE_POINTS];
    for (int i = 0; i < WAVE_POINTS; i++) {
        // Лінійне зростання від 0 до 255, потім скидання до 0 у наступному періоді
        sawtooth_buf[i] = (uint8_t)((i * 256) / WAVE_POINTS);
    }

    // Циклічна передача даних пилкоподібного сигналу (DMA автоматично повторює вивід)
    size_t bytes_loaded = 0;
    ESP_ERROR_CHECK(dac_continuous_write_cyclically(dac_handle, sawtooth_buf, sizeof(sawtooth_buf), &bytes_loaded));
    
    printf("Пилкоподібний сигнал запущено на GPIO%d, частота дискретизації %d Hz, частота сигналу %d Hz\n", 
           DAC_GPIO, SAMPLE_RATE, SAMPLE_RATE / WAVE_POINTS);

}

void sample_triangle()
{
   // Генерація даних трикутної хвилі (8-бітне розділення, 0~255)
    uint8_t tri_buf[WAVE_POINTS];
    for (int i = 0; i < WAVE_POINTS; i++) {
        if (i < WAVE_POINTS / 2) {
            // Перша половина: лінійне зростання 0 -> 255
            tri_buf[i] = (uint8_t)((i * 256) / (WAVE_POINTS / 2));
        } else {
            // Друга половина: лінійне спадання 255 -> 0
            tri_buf[i] = (uint8_t)(((WAVE_POINTS - i) * 256) / (WAVE_POINTS / 2));
        }
    }    

    // 2. Налаштування безперервного режиму DAC
    dac_continuous_config_t cont_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0,
        .desc_num  = 4,
        .buf_size  = 256,
        .freq_hz   = SAMPLE_RATE,
        .offset    = 0,
        .clk_src   = DAC_DIGI_CLK_SRC_DEFAULT,
    };
    dac_continuous_handle_t dac_handle;
    ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &dac_handle));
    ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));

    // 3. Циклічний вивід (DMA автоматично повторює)
    size_t bytes_loaded = 0;
    ESP_ERROR_CHECK(dac_continuous_write_cyclically(dac_handle, tri_buf, sizeof(tri_buf), &bytes_loaded));
    
    printf("Трикутна хвиля запущена на GPIO%d, частота дискретизації %d Hz, частота сигналу %d Hz\n", 
           DAC_GPIO, SAMPLE_RATE, SAMPLE_RATE / WAVE_POINTS);
}

void sample_cosine()
{
    // Налаштування косинусної хвилі (зміщення фази для отримання синусоїди)
    dac_cosine_config_t cos_cfg = {
        .chan_id = DAC_CHANNEL_MASK_CH0,     // GPIO25
        .freq_hz = 1000,                     // 1 кГц
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .atten = DAC_COSINE_ATTEN_DB_0,     // Повна амплітуда
        .phase = DAC_COSINE_PHASE_180,      // Зсув фази 180° (у поєднанні з налаштуванням можна отримати синусоїду)
        .offset = 0,
        .flags.force_set_freq = false,
    };
    dac_cosine_handle_t cos_handle;
    ESP_ERROR_CHECK(dac_cosine_new_channel(&cos_cfg, &cos_handle));
    ESP_ERROR_CHECK(dac_cosine_start(cos_handle));    
}

void sample_sine(void)
{
    uint8_t sine_buf[WAVE_POINTS];
    for (int i = 0; i < WAVE_POINTS; i++) {
        // Генерація синусоїди, амплітуда від 0 до 255
        sine_buf[i] = (uint8_t)(127.5 * (1 + sin(2 * M_PI * i / WAVE_POINTS)));
    }

    dac_continuous_config_t cont_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0,
        .desc_num = 4,
        .buf_size = 256,
        .freq_hz = SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
    };
    dac_continuous_handle_t dac_handle;
    ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &dac_handle));
    ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));

    size_t bytes_loaded;
    ESP_ERROR_CHECK(dac_continuous_write_cyclically(dac_handle, sine_buf, sizeof(sine_buf), &bytes_loaded));
}

void app_main(void)
{
    // приклад 1: пилка 
    //sample_saw();

    // приклад 2: трикутник
    //sample_triangle();

    // приклад 3: графік косінус (вбудована функція)
    //sample_cosine();

    // приклад 4: графік синус (за обчисленими значеннями)
    sample_sine();

    // Головний цикл не потребує втручання, DMA працює у фоновому режимі
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
