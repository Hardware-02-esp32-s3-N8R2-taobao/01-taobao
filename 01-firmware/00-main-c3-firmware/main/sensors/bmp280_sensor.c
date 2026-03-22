#include "bmp280_sensor.h"

#include <string.h>

#include "driver/i2c.h"
#include "esp_check.h"

#include "app_config.h"
#include "sensor_bus.h"

#define BMP280_REG_CHIP_ID    0xD0
#define BMP280_REG_RESET      0xE0
#define BMP280_REG_STATUS     0xF3
#define BMP280_REG_CTRL_HUM   0xF2
#define BMP280_REG_CTRL_MEAS  0xF4
#define BMP280_REG_CONFIG     0xF5
#define BMP280_REG_PRESS_MSB  0xF7
#define BMP280_REG_CALIB00    0x88
#define BME280_REG_CALIB26    0xE1
#define BMP280_CHIP_ID        0x58
#define BME280_CHIP_ID        0x60

typedef struct {
    uint16_t dig_t1;
    int16_t dig_t2;
    int16_t dig_t3;
    uint16_t dig_p1;
    int16_t dig_p2;
    int16_t dig_p3;
    int16_t dig_p4;
    int16_t dig_p5;
    int16_t dig_p6;
    int16_t dig_p7;
    int16_t dig_p8;
    int16_t dig_p9;
    uint8_t dig_h1;
    int16_t dig_h2;
    uint8_t dig_h3;
    int16_t dig_h4;
    int16_t dig_h5;
    int8_t dig_h6;
} bmp280_calibration_t;

static uint8_t s_addr = 0;
static uint8_t s_chip_id = 0;
static bool s_has_humidity = false;
static bmp280_calibration_t s_cal;
static bool s_cal_ready = false;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = {reg, value};
    return i2c_master_write_to_device(sensor_bus_i2c_port(), s_addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static esp_err_t read_regs(uint8_t reg, uint8_t *buffer, size_t len)
{
    return i2c_master_write_read_device(sensor_bus_i2c_port(), s_addr, &reg, 1, buffer, len, pdMS_TO_TICKS(100));
}

static esp_err_t probe_addr(uint8_t addr, uint8_t *chip_id)
{
    uint8_t reg = BMP280_REG_CHIP_ID;
    uint8_t value = 0;
    esp_err_t ret = i2c_master_write_read_device(sensor_bus_i2c_port(), addr, &reg, 1, &value, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }
    if (value == BMP280_CHIP_ID || value == BME280_CHIP_ID) {
        if (chip_id != NULL) {
            *chip_id = value;
        }
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static void load_u16_le(uint16_t *out, const uint8_t *buffer, int offset)
{
    *out = (uint16_t)(buffer[offset] | (buffer[offset + 1] << 8));
}

static void load_s16_le(int16_t *out, const uint8_t *buffer, int offset)
{
    *out = (int16_t)(buffer[offset] | (buffer[offset + 1] << 8));
}

static void load_bme280_humidity_calibration(const uint8_t *buffer)
{
    load_s16_le(&s_cal.dig_h2, buffer, 0);
    s_cal.dig_h3 = buffer[2];
    s_cal.dig_h4 = (int16_t)((buffer[3] << 4) | (buffer[4] & 0x0F));
    s_cal.dig_h5 = (int16_t)((buffer[5] << 4) | (buffer[4] >> 4));
    s_cal.dig_h6 = (int8_t)buffer[6];
}

esp_err_t bmp280_sensor_init(void)
{
    ESP_RETURN_ON_ERROR(sensor_bus_init(), "bmp280", "sensor bus init failed");

    uint8_t chip_id = 0;
    if (probe_addr(APP_BMP280_ADDR_PRIMARY, &chip_id) == ESP_OK) {
        s_addr = APP_BMP280_ADDR_PRIMARY;
    } else if (probe_addr(APP_BMP280_ADDR_SECONDARY, &chip_id) == ESP_OK) {
        s_addr = APP_BMP280_ADDR_SECONDARY;
    } else {
        s_addr = 0;
        s_chip_id = 0;
        s_has_humidity = false;
        s_cal_ready = false;
        return ESP_ERR_NOT_FOUND;
    }

    s_chip_id = chip_id;
    s_has_humidity = (chip_id == BME280_CHIP_ID);
    memset(&s_cal, 0, sizeof(s_cal));

    uint8_t calib[26] = {0};
    ESP_RETURN_ON_ERROR(read_regs(BMP280_REG_CALIB00, calib, sizeof(calib)), "bmp280", "read calib failed");
    load_u16_le(&s_cal.dig_t1, calib, 0);
    load_s16_le(&s_cal.dig_t2, calib, 2);
    load_s16_le(&s_cal.dig_t3, calib, 4);
    load_u16_le(&s_cal.dig_p1, calib, 6);
    load_s16_le(&s_cal.dig_p2, calib, 8);
    load_s16_le(&s_cal.dig_p3, calib, 10);
    load_s16_le(&s_cal.dig_p4, calib, 12);
    load_s16_le(&s_cal.dig_p5, calib, 14);
    load_s16_le(&s_cal.dig_p6, calib, 16);
    load_s16_le(&s_cal.dig_p7, calib, 18);
    load_s16_le(&s_cal.dig_p8, calib, 20);
    load_s16_le(&s_cal.dig_p9, calib, 22);
    s_cal.dig_h1 = calib[25];

    if (s_has_humidity) {
        uint8_t hcal[7] = {0};
        ESP_RETURN_ON_ERROR(read_regs(BME280_REG_CALIB26, hcal, sizeof(hcal)), "bmp280", "read humidity calib failed");
        load_bme280_humidity_calibration(hcal);
        ESP_RETURN_ON_ERROR(write_reg(BMP280_REG_CTRL_HUM, 0x01), "bmp280", "ctrl_hum failed");
    }

    ESP_RETURN_ON_ERROR(write_reg(BMP280_REG_CTRL_MEAS, 0x27), "bmp280", "ctrl_meas failed");
    ESP_RETURN_ON_ERROR(write_reg(BMP280_REG_CONFIG, 0xA0), "bmp280", "config failed");
    s_cal_ready = true;
    return ESP_OK;
}

esp_err_t bmp280_sensor_read(bmp280_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));

    if (!s_cal_ready) {
        esp_err_t init_ret = bmp280_sensor_init();
        if (init_ret != ESP_OK) {
            return init_ret;
        }
    }

    uint8_t data[8] = {0};
    size_t read_len = s_has_humidity ? sizeof(data) : 6;
    ESP_RETURN_ON_ERROR(read_regs(BMP280_REG_PRESS_MSB, data, read_len), "bmp280", "read data failed");

    int32_t adc_p = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
    int32_t adc_t = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));

    int32_t var1 = ((((adc_t >> 3) - ((int32_t)s_cal.dig_t1 << 1))) * ((int32_t)s_cal.dig_t2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - ((int32_t)s_cal.dig_t1)) * ((adc_t >> 4) - ((int32_t)s_cal.dig_t1))) >> 12) *
                    ((int32_t)s_cal.dig_t3)) >> 14;
    int32_t t_fine = var1 + var2;
    float temperature = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    int64_t p_var1 = ((int64_t)t_fine) - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)s_cal.dig_p6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)s_cal.dig_p5) << 17);
    p_var2 = p_var2 + (((int64_t)s_cal.dig_p4) << 35);
    p_var1 = ((p_var1 * p_var1 * (int64_t)s_cal.dig_p3) >> 8) + ((p_var1 * (int64_t)s_cal.dig_p2) << 12);
    p_var1 = (((((int64_t)1) << 47) + p_var1)) * ((int64_t)s_cal.dig_p1) >> 33;

    if (p_var1 == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    int64_t pressure = 1048576 - adc_p;
    pressure = (((pressure << 31) - p_var2) * 3125) / p_var1;
    p_var1 = (((int64_t)s_cal.dig_p9) * (pressure >> 13) * (pressure >> 13)) >> 25;
    p_var2 = (((int64_t)s_cal.dig_p8) * pressure) >> 19;
    pressure = ((pressure + p_var1 + p_var2) >> 8) + (((int64_t)s_cal.dig_p7) << 4);

    sample->ready = true;
    sample->address = s_addr;
    sample->chip_id = s_chip_id;
    sample->has_humidity = s_has_humidity;
    sample->temperature_c = temperature;
    sample->pressure_hpa = (float)pressure / 25600.0f;

    if (s_has_humidity) {
        int32_t adc_h = (int32_t)((data[6] << 8) | data[7]);
        int32_t v_x1 = t_fine - 76800;
        v_x1 = (((((adc_h << 14) - (((int32_t)s_cal.dig_h4) << 20) - (((int32_t)s_cal.dig_h5) * v_x1)) + 16384) >> 15) *
                (((((((v_x1 * ((int32_t)s_cal.dig_h6)) >> 10) * (((v_x1 * ((int32_t)s_cal.dig_h3)) >> 11) + 32768)) >> 10) + 2097152) *
                  ((int32_t)s_cal.dig_h2) + 8192) >> 14));
        v_x1 = v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((int32_t)s_cal.dig_h1)) >> 4);
        if (v_x1 < 0) {
            v_x1 = 0;
        }
        if (v_x1 > 419430400) {
            v_x1 = 419430400;
        }
        sample->humidity_pct = (float)(v_x1 >> 12) / 1024.0f;
    }

    return ESP_OK;
}
