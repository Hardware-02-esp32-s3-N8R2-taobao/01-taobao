#include "pressure/pressure_sensor.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/app_config.h"
#include "i2c_bus/i2c_bus.h"

#define BMP180_CHIP_ID 0x55
#define BMP280_CHIP_ID 0x58
#define BME280_CHIP_ID 0x60

typedef struct {
    int16_t ac1;
    int16_t ac2;
    int16_t ac3;
    uint16_t ac4;
    uint16_t ac5;
    uint16_t ac6;
    int16_t b1;
    int16_t b2;
    int16_t mb;
    int16_t mc;
    int16_t md;
} bmp180_calib_t;

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
} bmp280_calib_t;

static sensor_type_t s_pressure_type;
static uint8_t s_pressure_addr;
static bmp180_calib_t s_bmp180;
static bmp280_calib_t s_bmp280;
static int32_t s_bmp280_t_fine;
static float s_altitude_reference_pa;

static uint16_t read_le16(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint16_t read_be16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

const char *pressure_sensor_label(sensor_type_t type)
{
    switch (type) {
    case SENSOR_TYPE_BMP180:
        return "BMP180";
    case SENSOR_TYPE_BMP280:
        return "BMP280";
    case SENSOR_TYPE_BME280:
        return "BME280";
    default:
        return "PRESS";
    }
}

sensor_type_t pressure_sensor_type(void)
{
    return s_pressure_type;
}

static float pressure_to_altitude_m(float pressure_pa, float reference_pa)
{
    if (pressure_pa <= 0.0f || reference_pa <= 0.0f) {
        return 0.0f;
    }

    float altitude_m = 44330.0f * (1.0f - powf(pressure_pa / reference_pa, 0.1903f));
    if (fabsf(altitude_m) < 0.5f) {
        return 0.0f;
    }

    return altitude_m;
}

static esp_err_t bmp180_load_calibration(uint8_t addr)
{
    uint8_t raw[22];
    ESP_RETURN_ON_ERROR(i2c_bus_read_reg(addr, 0xAA, raw, sizeof(raw)), APP_TAG, "bmp180 calib read failed");

    s_bmp180.ac1 = (int16_t)read_be16(&raw[0]);
    s_bmp180.ac2 = (int16_t)read_be16(&raw[2]);
    s_bmp180.ac3 = (int16_t)read_be16(&raw[4]);
    s_bmp180.ac4 = read_be16(&raw[6]);
    s_bmp180.ac5 = read_be16(&raw[8]);
    s_bmp180.ac6 = read_be16(&raw[10]);
    s_bmp180.b1 = (int16_t)read_be16(&raw[12]);
    s_bmp180.b2 = (int16_t)read_be16(&raw[14]);
    s_bmp180.mb = (int16_t)read_be16(&raw[16]);
    s_bmp180.mc = (int16_t)read_be16(&raw[18]);
    s_bmp180.md = (int16_t)read_be16(&raw[20]);
    return ESP_OK;
}

static esp_err_t bmp180_read_sample(uint8_t addr, pressure_sample_t *sample)
{
    const int oss = 0;
    uint8_t raw[3];

    ESP_RETURN_ON_ERROR(i2c_bus_write_reg(addr, 0xF4, 0x2E), APP_TAG, "bmp180 temp trigger failed");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(i2c_bus_read_reg(addr, 0xF6, raw, 2), APP_TAG, "bmp180 temp read failed");
    int32_t ut = (int32_t)read_be16(raw);

    ESP_RETURN_ON_ERROR(i2c_bus_write_reg(addr, 0xF4, (uint8_t)(0x34 + (oss << 6))), APP_TAG, "bmp180 press trigger failed");
    vTaskDelay(pdMS_TO_TICKS(8));
    ESP_RETURN_ON_ERROR(i2c_bus_read_reg(addr, 0xF6, raw, 3), APP_TAG, "bmp180 press read failed");
    int32_t up = ((((int32_t)raw[0] << 16) | ((int32_t)raw[1] << 8) | raw[2]) >> (8 - oss));

    int32_t x1 = ((ut - (int32_t)s_bmp180.ac6) * (int32_t)s_bmp180.ac5) >> 15;
    int32_t x2 = ((int32_t)s_bmp180.mc << 11) / (x1 + s_bmp180.md);
    int32_t b5 = x1 + x2;
    int32_t temp_x10 = (b5 + 8) >> 4;

    int32_t b6 = b5 - 4000;
    x1 = ((int32_t)s_bmp180.b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = ((int32_t)s_bmp180.ac2 * b6) >> 11;
    int32_t x3 = x1 + x2;
    int32_t b3 = ((((int32_t)s_bmp180.ac1 * 4 + x3) << oss) + 2) >> 2;

    x1 = ((int32_t)s_bmp180.ac3 * b6) >> 13;
    x2 = ((int32_t)s_bmp180.b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    uint32_t b4 = ((uint32_t)s_bmp180.ac4 * (uint32_t)(x3 + 32768)) >> 15;
    uint32_t b7 = ((uint32_t)up - (uint32_t)b3) * (uint32_t)(50000 >> oss);

    int32_t pressure_pa;
    if (b7 < 0x80000000UL) {
        pressure_pa = (int32_t)((b7 << 1) / b4);
    } else {
        pressure_pa = (int32_t)((b7 / b4) << 1);
    }

    x1 = (pressure_pa >> 8) * (pressure_pa >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * pressure_pa) >> 16;
    pressure_pa += (x1 + x2 + 3791) >> 4;

    sample->ready = true;
    sample->temperature_c = temp_x10 / 10.0f;
    sample->pressure_pa = (float)pressure_pa;
    sample->pressure_hpa = sample->pressure_pa / 100.0f;
    if (s_altitude_reference_pa <= 0.0f) {
        s_altitude_reference_pa = sample->pressure_pa;
    }
    sample->altitude_m = pressure_to_altitude_m(sample->pressure_pa, s_altitude_reference_pa);
    return ESP_OK;
}

static esp_err_t bmp280_load_calibration(uint8_t addr)
{
    uint8_t raw[24];
    ESP_RETURN_ON_ERROR(i2c_bus_read_reg(addr, 0x88, raw, sizeof(raw)), APP_TAG, "bmp280 calib read failed");

    s_bmp280.dig_t1 = read_le16(&raw[0]);
    s_bmp280.dig_t2 = (int16_t)read_le16(&raw[2]);
    s_bmp280.dig_t3 = (int16_t)read_le16(&raw[4]);
    s_bmp280.dig_p1 = read_le16(&raw[6]);
    s_bmp280.dig_p2 = (int16_t)read_le16(&raw[8]);
    s_bmp280.dig_p3 = (int16_t)read_le16(&raw[10]);
    s_bmp280.dig_p4 = (int16_t)read_le16(&raw[12]);
    s_bmp280.dig_p5 = (int16_t)read_le16(&raw[14]);
    s_bmp280.dig_p6 = (int16_t)read_le16(&raw[16]);
    s_bmp280.dig_p7 = (int16_t)read_le16(&raw[18]);
    s_bmp280.dig_p8 = (int16_t)read_le16(&raw[20]);
    s_bmp280.dig_p9 = (int16_t)read_le16(&raw[22]);
    return ESP_OK;
}

static esp_err_t bmp280_configure(uint8_t addr, sensor_type_t type)
{
    if (type == SENSOR_TYPE_BME280) {
        ESP_RETURN_ON_ERROR(i2c_bus_write_reg(addr, 0xF2, 0x01), APP_TAG, "bme280 ctrl_hum write failed");
    }

    ESP_RETURN_ON_ERROR(i2c_bus_write_reg(addr, 0xF5, 0x00), APP_TAG, "bmp280 config write failed");
    ESP_RETURN_ON_ERROR(i2c_bus_write_reg(addr, 0xF4, 0x27), APP_TAG, "bmp280 ctrl_meas write failed");
    return ESP_OK;
}

static int32_t bmp280_compensate_temp(int32_t adc_t)
{
    int32_t var1 = ((((adc_t >> 3) - ((int32_t)s_bmp280.dig_t1 << 1))) * (int32_t)s_bmp280.dig_t2) >> 11;
    int32_t var2 = (((((adc_t >> 4) - (int32_t)s_bmp280.dig_t1) * ((adc_t >> 4) - (int32_t)s_bmp280.dig_t1)) >> 12) *
                    (int32_t)s_bmp280.dig_t3) >> 14;
    s_bmp280_t_fine = var1 + var2;
    return (s_bmp280_t_fine * 5 + 128) >> 8;
}

static uint32_t bmp280_compensate_pressure(int32_t adc_p)
{
    int64_t var1 = (int64_t)s_bmp280_t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s_bmp280.dig_p6;
    var2 += (var1 * (int64_t)s_bmp280.dig_p5) << 17;
    var2 += ((int64_t)s_bmp280.dig_p4) << 35;
    var1 = ((var1 * var1 * (int64_t)s_bmp280.dig_p3) >> 8) + ((var1 * (int64_t)s_bmp280.dig_p2) << 12);
    var1 = ((((int64_t)1) << 47) + var1) * (int64_t)s_bmp280.dig_p1 >> 33;

    if (var1 == 0) {
        return 0;
    }

    int64_t p = 1048576 - adc_p;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)s_bmp280.dig_p9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)s_bmp280.dig_p8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s_bmp280.dig_p7) << 4);
    return (uint32_t)p;
}

static esp_err_t bmp280_read_sample(uint8_t addr, pressure_sample_t *sample)
{
    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(i2c_bus_read_reg(addr, 0xF7, raw, sizeof(raw)), APP_TAG, "bmp280 sample read failed");

    int32_t adc_p = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_t = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);

    int32_t temp_x100 = bmp280_compensate_temp(adc_t);
    uint32_t pressure_q24_8 = bmp280_compensate_pressure(adc_p);

    sample->ready = true;
    sample->temperature_c = temp_x100 / 100.0f;
    sample->pressure_pa = pressure_q24_8 / 256.0f;
    sample->pressure_hpa = sample->pressure_pa / 100.0f;
    if (s_altitude_reference_pa <= 0.0f) {
        s_altitude_reference_pa = sample->pressure_pa;
    }
    sample->altitude_m = pressure_to_altitude_m(sample->pressure_pa, s_altitude_reference_pa);
    return ESP_OK;
}

esp_err_t pressure_sensor_init(void)
{
    const uint8_t candidate_addrs[] = {0x76, 0x77};

    s_pressure_type = SENSOR_TYPE_NONE;
    s_pressure_addr = 0;
    s_altitude_reference_pa = 0.0f;

    for (size_t i = 0; i < sizeof(candidate_addrs); i++) {
        uint8_t addr = candidate_addrs[i];
        if (i2c_bus_probe(addr) != ESP_OK) {
            continue;
        }

        uint8_t chip_id = 0;
        if (i2c_bus_read_reg(addr, 0xD0, &chip_id, 1) != ESP_OK) {
            continue;
        }

        if (chip_id == BMP180_CHIP_ID) {
            ESP_RETURN_ON_ERROR(bmp180_load_calibration(addr), APP_TAG, "bmp180 calibration load failed");
            s_pressure_type = SENSOR_TYPE_BMP180;
            s_pressure_addr = addr;
            return ESP_OK;
        }

        if (chip_id == BMP280_CHIP_ID || chip_id == BME280_CHIP_ID) {
            sensor_type_t type = chip_id == BME280_CHIP_ID ? SENSOR_TYPE_BME280 : SENSOR_TYPE_BMP280;
            ESP_RETURN_ON_ERROR(bmp280_load_calibration(addr), APP_TAG, "bmp280 calibration load failed");
            ESP_RETURN_ON_ERROR(bmp280_configure(addr, type), APP_TAG, "bmp280 configure failed");
            s_pressure_type = type;
            s_pressure_addr = addr;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t pressure_sensor_read(pressure_sample_t *sample)
{
    memset(sample, 0, sizeof(*sample));

    switch (s_pressure_type) {
    case SENSOR_TYPE_BMP180:
        return bmp180_read_sample(s_pressure_addr, sample);
    case SENSOR_TYPE_BMP280:
    case SENSOR_TYPE_BME280:
        return bmp280_read_sample(s_pressure_addr, sample);
    default:
        return ESP_ERR_INVALID_STATE;
    }
}
