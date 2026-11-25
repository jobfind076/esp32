#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"

// Регістри BME280
#define REG_ID 0xD0
#define REG_RESET 0xE0
#define REG_CTRL_HUM 0xF2
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG 0xF5
#define REG_PRESS_MSB 0xF7
#define REG_CALIB_00 0x88
#define REG_CALIB_26 0xE1

// Структура для калібрувальних даних датчика
typedef struct
{
    uint16_t dig_T1;
    int16_t dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t dig_H1, dig_H3;
    int16_t dig_H2, dig_H4, dig_H5;
    int8_t dig_H6;
} bme280_calib_t;

esp_err_t bme280_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val);
esp_err_t bme280_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len);
void bme280_read_calibration(i2c_master_dev_handle_t dev);
float compensate_temp(int32_t adc_T);
float compensate_press(int32_t adc_P);
float compensate_hum(int32_t adc_H);


