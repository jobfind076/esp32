#include "bme280.h"

static bme280_calib_t calib;
static int32_t t_fine;

// Допоміжні функції читання/запису через новий i2c_master API
esp_err_t bme280_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t data[2] = {reg, val};
    return i2c_master_transmit(dev, data, sizeof(data), -1);
}

esp_err_t bme280_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, -1);
}

// Зчитування заводських калібрувальних коефіцієнтів
void bme280_read_calibration(i2c_master_dev_handle_t dev)
{
    uint8_t b1[26], b2[7];
    bme280_read_regs(dev, REG_CALIB_00, b1, 26);
    bme280_read_regs(dev, REG_CALIB_26, b2, 7);

    calib.dig_T1 = (b1[1] << 8) | b1[0];
    calib.dig_T2 = (b1[3] << 8) | b1[2];
    calib.dig_T3 = (b1[5] << 8) | b1[4];

    calib.dig_P1 = (b1[7] << 8) | b1[6];
    calib.dig_P2 = (b1[9] << 8) | b1[8];
    calib.dig_P3 = (b1[11] << 8) | b1[10];
    calib.dig_P4 = (b1[13] << 8) | b1[12];
    calib.dig_P5 = (b1[15] << 8) | b1[14];
    calib.dig_P6 = (b1[17] << 8) | b1[16];
    calib.dig_P7 = (b1[19] << 8) | b1[18];
    calib.dig_P8 = (b1[21] << 8) | b1[20];
    calib.dig_P9 = (b1[23] << 8) | b1[22];

    calib.dig_H1 = b1[25];
    calib.dig_H2 = (b2[1] << 8) | b2[0];
    calib.dig_H3 = b2[2];
    calib.dig_H4 = (b2[3] << 4) | (b2[4] & 0x0F);
    calib.dig_H5 = (b2[5] << 4) | (b2[4] >> 4);
    calib.dig_H6 = (int8_t)b2[6];
}

// Формули компенсації з даташиту Bosch BME280
float compensate_temp(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calib.dig_T1 << 1))) * ((int32_t)calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)calib.dig_T1))) >> 12) *
                    ((int32_t)calib.dig_T3)) >>
                   14;
    t_fine = var1 + var2;
    return ((t_fine * 5 + 128) >> 8) / 100.0f;
}

float compensate_press(int32_t adc_P)
{
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib.dig_P3) >> 8) + ((var1 * (int64_t)calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calib.dig_P1) >> 33;
    if (var1 == 0)
        return 0.0f;
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib.dig_P7) << 4);
    return (p / 256.0f) / 100.0f; // у гПа (hPa)
}

float compensate_hum(int32_t adc_H)
{
    int32_t v_x1 = (t_fine - ((int32_t)76800));
    v_x1 =
        (((((adc_H << 14) - (((int32_t)calib.dig_H4) << 20) - (((int32_t)calib.dig_H5) * v_x1)) + ((int32_t)16384)) >>
          15) *
         (((((((v_x1 * ((int32_t)calib.dig_H6)) >> 10) *
              (((v_x1 * ((int32_t)calib.dig_H3)) >> 11) + ((int32_t)32768))) >>
             10) +
            ((int32_t)2097152)) *
               ((int32_t)calib.dig_H2) +
           8192) >>
          14));
    v_x1 = (v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((int32_t)calib.dig_H1)) >> 4));
    v_x1 = (v_x1 < 0 ? 0 : v_x1);
    v_x1 = (v_x1 > 419430400 ? 419430400 : v_x1);
    return (v_x1 >> 12) / 1024.0f;
}
