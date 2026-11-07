#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

static const char* TAG = "NTC_EXAMPLE";

// ================== Налаштування апаратного забезпечення ==================
#define NTC_GPIO_CHANNEL ADC_CHANNEL_3 // GPIO4 (залежить від моделі, перевірте datasheet)
#define ADC_UNIT_USED ADC_UNIT_1
#define ADC_ATTEN_USED ADC_ATTEN_DB_12 // Діапазон вимірювання до ~2450mV, уникання насичення
#define ADC_BITWIDTH_USED ADC_BITWIDTH_12

// ================== Параметри схеми ==================
#define SERIES_RESISTOR 10000.0f // Послідовний резистор 10K
#define VREF_MV 3300.0f // Напруга живлення

// ================== Таблиця пошуку (R25=10K, B=3950) ==================
// Формат: {Температура (°C), Опір (Ом)}, від високої до низької температури (опір зменшується)
typedef struct {
    float temp;
    float resistance;
} ntc_point_t;

// Дані отримані з типової кривої B=3950 (можна замінити на таблицю від виробника)
static const ntc_point_t ntc_table[] = {
    { -20.0f, 97070.0f },
    { -10.0f, 55326.0f },
    {   0.0f, 32650.0f },
    {  10.0f, 19900.0f },
    {  20.0f, 12488.0f },
    {  25.0f, 10000.0f },
    {  30.0f,  8057.0f },
    {  40.0f,  5327.0f },
    {  50.0f,  3603.0f },
    {  60.0f,  2488.0f },
    {  70.0f,  1752.0f },
    {  80.0f,  1256.0f },
    {  90.0f,   914.0f },
    { 100.0f,   672.0f },
};

#define TABLE_SIZE (sizeof(ntc_table) / sizeof(ntc_table[0]))

// Функція лінійної інтерполяції
// Вхід: опір NTC (Ом)
// Вихід: температура (°C)
float resistance_to_temperature(float resistance)
{
    // 1. Обробка граничних випадків
    if (resistance >= ntc_table[0].resistance) 
    {
        return ntc_table[0].temp;
    }

    if (resistance <= ntc_table[TABLE_SIZE - 1].resistance) 
    {
        return ntc_table[TABLE_SIZE - 1].temp;
    }

    // 2. Пошук інтервалу (лінійний пошук, оскільки таблиця невелика)
    // Оскільки опір зменшується з підвищенням температури, шукаємо першу точку, де опір менший за поточний
    for (int i = 0; i < TABLE_SIZE - 1; i++) 
    {
        if (resistance >= ntc_table[i + 1].resistance) 
        {
            // Знайдено інтервал: між i та i+1
            float r_high = ntc_table[i].resistance; // Більший опір (нижча температура)
            float r_low = ntc_table[i + 1].resistance; // Менший опір (вища температура)
            float t_high = ntc_table[i].temp;
            float t_low = ntc_table[i + 1].temp;

            // Формула лінійної інтерполяції
            // temp = t_high + (t_low - t_high) * (r_high - resistance) / (r_high - r_low)
            float ratio = (r_high - resistance) / (r_high - r_low);
            return t_high + ratio * (t_low - t_high);
        }
    }
    return 0.0f; // Теоретично недосяжно
}

void app_main(void)
{
    // ================== Ініціалізація АЦП ==================
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_USED,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_USED,
        .bitwidth = ADC_BITWIDTH_USED,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, NTC_GPIO_CHANNEL, &chan_config));

    ESP_LOGI(TAG, "NTC приклад запущено, початок вимірювання...");

    while (1) 
    {
        int raw_value = 0;
        // Зчитування одного разу
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, NTC_GPIO_CHANNEL, &raw_value));

        // ================== Перетворення напруги та опору ==================
        // 1. Сира величина -> напруга (мВ)
        // Припущення: лінійне перетворення (для підвищення точності рекомендується використовувати adc_cali)
        float voltage_mv = (raw_value / 4095.0f) * VREF_MV;

        // 2. Напруга -> опір NTC
        // Формула: R_ntc = R_series * V_ntc / (V_ref - V_ntc)
        // Увага: тут припускається, що NTC підключений до GND, послідовний резистор до 3.3V
        // Якщо напруга дуже близька до VREF або 0, виникає ділення на нуль або від'ємне значення
        float resistance = 0.0f;
        if (voltage_mv > 0.0f && voltage_mv < VREF_MV) 
        {
            resistance = SERIES_RESISTOR * voltage_mv / (VREF_MV - voltage_mv);
        } 
        else 
        {
            ESP_LOGW(TAG, "Помилка діапазону напруги: %.2f mV", voltage_mv);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // ================== Обчислення температури ==================
        float temperature = resistance_to_temperature(resistance);

        // Виведення результату
        ESP_LOGI(TAG, "Сира величина: %4d | Напруга: %6.1f mV | Опір: %8.1f Ω | Температура: %6.2f °C",
            raw_value, voltage_mv, resistance, temperature);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}