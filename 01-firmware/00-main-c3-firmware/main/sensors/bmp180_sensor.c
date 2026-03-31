#include "bmp180_sensor.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "sensor_bus.h"

#define BMP180_REG_CHIP_ID       0xD0
#define BMP180_REG_CALIB_START   0xAA
#define BMP180_REG_CONTROL       0xF4
#define BMP180_REG_RESULT        0xF6
#define BMP180_CMD_TEMP          0x2E
#define BMP180_CMD_PRESSURE      0x34
#define BMP180_OSS               3

#define BMP280_REG_CHIP_ID       0xD0
#define BMP280_REG_CTRL_MEAS     0xF4
#define BMP280_REG_CONFIG        0xF5
#define BMP280_REG_PRESS_MSB     0xF7
#define BMP280_REG_CALIB00       0x88

typedef enum {
    BMPX80_MODEL_NONE = 0,
    BMPX80_MODEL_BMP180,
    BMPX80_MODEL_BMP280,
} bmpx80_model_t;

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
} bmp180_calibration_t;

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
} bmp280_calibration_t;

typedef struct {
    int32_t raw_temp;
    int32_t raw_press;
} bmp_raw_reading_t;

static uint8_t s_addr = 0;
static uint8_t s_chip_id = 0;
static bmpx80_model_t s_model = BMPX80_MODEL_NONE;
static bool s_cal_ready = false;
static bmp180_calibration_t s_bmp180_cal = {0};
static bmp280_calibration_t s_bmp280_cal = {0};

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = { reg, value };
    return i2c_master_write_to_device(sensor_bus_i2c_port(), s_addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static esp_err_t read_regs(uint8_t reg, uint8_t *buffer, size_t len)
{
    return i2c_master_write_read_device(sensor_bus_i2c_port(), s_addr, &reg, 1, buffer, len, pdMS_TO_TICKS(100));
}

static esp_err_t probe_addr(uint8_t addr, uint8_t *chip_id)
{
    uint8_t reg = BMP180_REG_CHIP_ID;
    uint8_t value = 0;
    esp_err_t ret = i2c_master_write_read_device(sensor_bus_i2c_port(), addr, &reg, 1, &value, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }
    if (value != BMP180_CHIP_ID && value != BMP280_CHIP_ID) {
        return ESP_ERR_NOT_FOUND;
    }
    if (chip_id != NULL) {
        *chip_id = value;
    }
    return ESP_OK;
}

static int16_t load_s16_be(const uint8_t *buffer, size_t offset)
{
    return (int16_t)((buffer[offset] << 8) | buffer[offset + 1]);
}

static uint16_t load_u16_be(const uint8_t *buffer, size_t offset)
{
    return (uint16_t)((buffer[offset] << 8) | buffer[offset + 1]);
}

static void load_u16_le(uint16_t *out, const uint8_t *buffer, int offset)
{
    *out = (uint16_t)(buffer[offset] | (buffer[offset + 1] << 8));
}

static void load_s16_le(int16_t *out, const uint8_t *buffer, int offset)
{
    *out = (int16_t)(buffer[offset] | (buffer[offset + 1] << 8));
}

static esp_err_t init_bmp180_calibration(void)
{
    uint8_t calib[22] = {0};
    ESP_RETURN_ON_ERROR(read_regs(BMP180_REG_CALIB_START, calib, sizeof(calib)), "bmpx80", "bmp180 calib read failed");

    s_bmp180_cal.ac1 = load_s16_be(calib, 0);
    s_bmp180_cal.ac2 = load_s16_be(calib, 2);
    s_bmp180_cal.ac3 = load_s16_be(calib, 4);
    s_bmp180_cal.ac4 = load_u16_be(calib, 6);
    s_bmp180_cal.ac5 = load_u16_be(calib, 8);
    s_bmp180_cal.ac6 = load_u16_be(calib, 10);
    s_bmp180_cal.b1 = load_s16_be(calib, 12);
    s_bmp180_cal.b2 = load_s16_be(calib, 14);
    s_bmp180_cal.mb = load_s16_be(calib, 16);
    s_bmp180_cal.mc = load_s16_be(calib, 18);
    s_bmp180_cal.md = load_s16_be(calib, 20);
    return ESP_OK;
}

static esp_err_t init_bmp280_calibration(void)
{
    uint8_t calib[24] = {0};
    ESP_RETURN_ON_ERROR(read_regs(BMP280_REG_CALIB00, calib, sizeof(calib)), "bmpx80", "bmp280 calib read failed");

    load_u16_le(&s_bmp280_cal.dig_t1, calib, 0);
    load_s16_le(&s_bmp280_cal.dig_t2, calib, 2);
    load_s16_le(&s_bmp280_cal.dig_t3, calib, 4);
    load_u16_le(&s_bmp280_cal.dig_p1, calib, 6);
    load_s16_le(&s_bmp280_cal.dig_p2, calib, 8);
    load_s16_le(&s_bmp280_cal.dig_p3, calib, 10);
    load_s16_le(&s_bmp280_cal.dig_p4, calib, 12);
    load_s16_le(&s_bmp280_cal.dig_p5, calib, 14);
    load_s16_le(&s_bmp280_cal.dig_p6, calib, 16);
    load_s16_le(&s_bmp280_cal.dig_p7, calib, 18);
    load_s16_le(&s_bmp280_cal.dig_p8, calib, 20);
    load_s16_le(&s_bmp280_cal.dig_p9, calib, 22);

    ESP_RETURN_ON_ERROR(write_reg(BMP280_REG_CTRL_MEAS, 0x27), "bmpx80", "bmp280 ctrl_meas failed");
    ESP_RETURN_ON_ERROR(write_reg(BMP280_REG_CONFIG, 0xA0), "bmpx80", "bmp280 config failed");
    return ESP_OK;
}

static esp_err_t read_bmp180_raw(bmp_raw_reading_t *raw)
{
    uint8_t data[3] = {0};

    ESP_RETURN_ON_ERROR(write_reg(BMP180_REG_CONTROL, BMP180_CMD_TEMP), "bmpx80", "bmp180 start temp failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(read_regs(BMP180_REG_RESULT, data, 2), "bmpx80", "bmp180 temp read failed");
    raw->raw_temp = (int32_t)((data[0] << 8) | data[1]);

    ESP_RETURN_ON_ERROR(
        write_reg(BMP180_REG_CONTROL, (uint8_t)(BMP180_CMD_PRESSURE + (BMP180_OSS << 6))),
        "bmpx80",
        "bmp180 start press failed"
    );
    vTaskDelay(pdMS_TO_TICKS(40));
    ESP_RETURN_ON_ERROR(read_regs(BMP180_REG_RESULT, data, 3), "bmpx80", "bmp180 press read failed");
    raw->raw_press = (int32_t)((((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2]) >> (8 - BMP180_OSS));
    return ESP_OK;
}

static esp_err_t read_bmp280_raw(bmp_raw_reading_t *raw)
{
    uint8_t data[6] = {0};
    ESP_RETURN_ON_ERROR(read_regs(BMP280_REG_PRESS_MSB, data, sizeof(data)), "bmpx80", "bmp280 raw read failed");
    raw->raw_press = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
    raw->raw_temp = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));
    return ESP_OK;
}

static esp_err_t compensate_bmp180(const bmp_raw_reading_t *raw, bmp180_sample_t *sample)
{
    int32_t x1 = ((raw->raw_temp - (int32_t)s_bmp180_cal.ac6) * (int32_t)s_bmp180_cal.ac5) >> 15;
    int32_t x2 = ((int32_t)s_bmp180_cal.mc << 11) / (x1 + (int32_t)s_bmp180_cal.md);
    int32_t b5 = x1 + x2;
    int32_t temperature = (b5 + 8) >> 4;

    int32_t b6 = b5 - 4000;
    x1 = ((int32_t)s_bmp180_cal.b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = ((int32_t)s_bmp180_cal.ac2 * b6) >> 11;
    int32_t x3 = x1 + x2;
    int32_t b3 = ((((int32_t)s_bmp180_cal.ac1 * 4 + x3) << BMP180_OSS) + 2) >> 2;

    x1 = ((int32_t)s_bmp180_cal.ac3 * b6) >> 13;
    x2 = ((int32_t)s_bmp180_cal.b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    uint32_t b4 = ((uint32_t)s_bmp180_cal.ac4 * (uint32_t)(x3 + 32768)) >> 15;
    if (b4 == 0U) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint32_t b7 = ((uint32_t)raw->raw_press - (uint32_t)b3) * (uint32_t)(50000 >> BMP180_OSS);

    int32_t pressure = (b7 < 0x80000000U)
        ? (int32_t)((b7 * 2U) / b4)
        : (int32_t)((b7 / b4) * 2U);

    x1 = (pressure >> 8) * (pressure >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * pressure) >> 16;
    pressure += (x1 + x2 + 3791) >> 4;

    sample->temperature_c = (float)temperature / 10.0f;
    sample->pressure_hpa = (float)pressure / 100.0f;
    return ESP_OK;
}

static esp_err_t compensate_bmp280(const bmp_raw_reading_t *raw, bmp180_sample_t *sample)
{
    int32_t adc_t = raw->raw_temp;
    int32_t adc_p = raw->raw_press;

    int32_t var1 = ((((adc_t >> 3) - ((int32_t)s_bmp280_cal.dig_t1 << 1))) * ((int32_t)s_bmp280_cal.dig_t2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - ((int32_t)s_bmp280_cal.dig_t1)) * ((adc_t >> 4) - ((int32_t)s_bmp280_cal.dig_t1))) >> 12) *
                    ((int32_t)s_bmp280_cal.dig_t3)) >> 14;
    int32_t t_fine = var1 + var2;
    float temperature = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    int64_t p_var1 = ((int64_t)t_fine) - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)s_bmp280_cal.dig_p6;
    p_var2 += (p_var1 * (int64_t)s_bmp280_cal.dig_p5) << 17;
    p_var2 += ((int64_t)s_bmp280_cal.dig_p4) << 35;
    p_var1 = ((p_var1 * p_var1 * (int64_t)s_bmp280_cal.dig_p3) >> 8) + ((p_var1 * (int64_t)s_bmp280_cal.dig_p2) << 12);
    p_var1 = (((((int64_t)1) << 47) + p_var1)) * ((int64_t)s_bmp280_cal.dig_p1) >> 33;
    if (p_var1 == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    int64_t pressure = 1048576 - adc_p;
    pressure = (((pressure << 31) - p_var2) * 3125) / p_var1;
    p_var1 = (((int64_t)s_bmp280_cal.dig_p9) * (pressure >> 13) * (pressure >> 13)) >> 25;
    p_var2 = (((int64_t)s_bmp280_cal.dig_p8) * pressure) >> 19;
    pressure = ((pressure + p_var1 + p_var2) >> 8) + (((int64_t)s_bmp280_cal.dig_p7) << 4);

    sample->temperature_c = temperature;
    sample->pressure_hpa = (float)pressure / 25600.0f;
    return ESP_OK;
}

esp_err_t bmp180_sensor_init(void)
{
    ESP_RETURN_ON_ERROR(sensor_bus_init(), "bmpx80", "sensor bus init failed");

    uint8_t chip_id = 0;
    if (probe_addr(APP_BMP180_ADDR_PRIMARY, &chip_id) == ESP_OK) {
        s_addr = APP_BMP180_ADDR_PRIMARY;
    } else if (probe_addr(APP_BMP180_ADDR_SECONDARY, &chip_id) == ESP_OK) {
        s_addr = APP_BMP180_ADDR_SECONDARY;
    } else {
        s_addr = 0;
        s_chip_id = 0;
        s_model = BMPX80_MODEL_NONE;
        s_cal_ready = false;
        return ESP_ERR_NOT_FOUND;
    }

    s_chip_id = chip_id;
    if (chip_id == BMP180_CHIP_ID) {
        s_model = BMPX80_MODEL_BMP180;
        ESP_RETURN_ON_ERROR(init_bmp180_calibration(), "bmpx80", "bmp180 init failed");
    } else if (chip_id == BMP280_CHIP_ID) {
        s_model = BMPX80_MODEL_BMP280;
        ESP_RETURN_ON_ERROR(init_bmp280_calibration(), "bmpx80", "bmp280 init failed");
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_cal_ready = true;
    return ESP_OK;
}

esp_err_t bmp180_sensor_read(bmp180_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));
    if (!s_cal_ready) {
        ESP_RETURN_ON_ERROR(bmp180_sensor_init(), "bmpx80", "init failed");
    }

    bmp_raw_reading_t raw = {0};
    esp_err_t ret = (s_model == BMPX80_MODEL_BMP280) ? read_bmp280_raw(&raw) : read_bmp180_raw(&raw);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = (s_model == BMPX80_MODEL_BMP280) ? compensate_bmp280(&raw, sample) : compensate_bmp180(&raw, sample);
    if (ret != ESP_OK) {
        return ret;
    }

    sample->ready = true;
    sample->address = s_addr;
    sample->chip_id = s_chip_id;
    return ESP_OK;
}

esp_err_t bmp180_sensor_build_debug_json(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_cal_ready) {
        esp_err_t init_ret = bmp180_sensor_init();
        if (init_ret != ESP_OK) {
            snprintf(buffer, buffer_size, "{\"error\":\"%s\"}", esp_err_to_name(init_ret));
            return init_ret;
        }
    }

    bmp_raw_reading_t raw = {0};
    esp_err_t ret = (s_model == BMPX80_MODEL_BMP280) ? read_bmp280_raw(&raw) : read_bmp180_raw(&raw);
    if (ret != ESP_OK) {
        snprintf(buffer, buffer_size, "{\"error\":\"%s\"}", esp_err_to_name(ret));
        return ret;
    }

    bmp180_sample_t sample = {0};
    ret = (s_model == BMPX80_MODEL_BMP280) ? compensate_bmp280(&raw, &sample) : compensate_bmp180(&raw, &sample);
    if (ret != ESP_OK) {
        snprintf(buffer, buffer_size, "{\"error\":\"%s\"}", esp_err_to_name(ret));
        return ret;
    }

    if (s_model == BMPX80_MODEL_BMP280) {
        snprintf(
            buffer,
            buffer_size,
            "{\"model\":\"bmp280\",\"address\":%u,\"chipId\":%u,\"rawTemp\":%ld,\"rawPress\":%ld,"
            "\"digT1\":%u,\"digT2\":%d,\"digT3\":%d,\"digP1\":%u,\"digP2\":%d,\"digP3\":%d,\"digP4\":%d,"
            "\"digP5\":%d,\"digP6\":%d,\"digP7\":%d,\"digP8\":%d,\"digP9\":%d,"
            "\"temperature\":%.2f,\"pressure\":%.2f}",
            s_addr,
            s_chip_id,
            (long)raw.raw_temp,
            (long)raw.raw_press,
            (unsigned int)s_bmp280_cal.dig_t1,
            (int)s_bmp280_cal.dig_t2,
            (int)s_bmp280_cal.dig_t3,
            (unsigned int)s_bmp280_cal.dig_p1,
            (int)s_bmp280_cal.dig_p2,
            (int)s_bmp280_cal.dig_p3,
            (int)s_bmp280_cal.dig_p4,
            (int)s_bmp280_cal.dig_p5,
            (int)s_bmp280_cal.dig_p6,
            (int)s_bmp280_cal.dig_p7,
            (int)s_bmp280_cal.dig_p8,
            (int)s_bmp280_cal.dig_p9,
            sample.temperature_c,
            sample.pressure_hpa
        );
    } else {
        snprintf(
            buffer,
            buffer_size,
            "{\"model\":\"bmp180\",\"address\":%u,\"chipId\":%u,\"ut\":%ld,\"up\":%ld,\"oss\":%d,"
            "\"ac1\":%d,\"ac2\":%d,\"ac3\":%d,\"ac4\":%u,\"ac5\":%u,\"ac6\":%u,"
            "\"b1\":%d,\"b2\":%d,\"mb\":%d,\"mc\":%d,\"md\":%d,"
            "\"temperature\":%.2f,\"pressure\":%.2f}",
            s_addr,
            s_chip_id,
            (long)raw.raw_temp,
            (long)raw.raw_press,
            BMP180_OSS,
            (int)s_bmp180_cal.ac1,
            (int)s_bmp180_cal.ac2,
            (int)s_bmp180_cal.ac3,
            (unsigned int)s_bmp180_cal.ac4,
            (unsigned int)s_bmp180_cal.ac5,
            (unsigned int)s_bmp180_cal.ac6,
            (int)s_bmp180_cal.b1,
            (int)s_bmp180_cal.b2,
            (int)s_bmp180_cal.mb,
            (int)s_bmp180_cal.mc,
            (int)s_bmp180_cal.md,
            sample.temperature_c,
            sample.pressure_hpa
        );
    }

    return ESP_OK;
}
