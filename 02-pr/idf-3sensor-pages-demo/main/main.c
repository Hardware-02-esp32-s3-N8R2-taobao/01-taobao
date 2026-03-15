#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define TAG "env_pages"

#define I2C_HOST                I2C_NUM_0
#define I2C_SDA_GPIO            5
#define I2C_SCL_GPIO            6
#define I2C_CLOCK_HZ            (400 * 1000)
#define PAGE_REFRESH_MS         3000
#define RGB_LED_GPIO            48
#define RGB_LED_COUNT           1
#define RGB_RMT_RESOLUTION_HZ   (10 * 1000 * 1000)

#define OLED_I2C_ADDR_PRIMARY   0x3C
#define OLED_I2C_ADDR_SECONDARY 0x3D
#define OLED_H_RES              128
#define OLED_V_RES              64

#define DS18B20_GPIO            4
#define DS18B20_CMD_SKIP_ROM    0xCC
#define DS18B20_CMD_CONVERT_T   0x44
#define DS18B20_CMD_READ_PAD    0xBE

#define DHT11_GPIO              7

#define BMP180_CHIP_ID          0x55
#define BMP280_CHIP_ID          0x58
#define BME280_CHIP_ID          0x60

typedef enum {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_BMP180,
    SENSOR_TYPE_BMP280,
    SENSOR_TYPE_BME280,
} sensor_type_t;

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

typedef struct {
    bool ready;
    float temperature_c;
    float pressure_pa;
    float pressure_hpa;
    float altitude_m;
} pressure_sample_t;

typedef struct {
    bool ready;
    float temperature_c;
} ds18b20_sample_t;

typedef struct {
    bool ready;
    int temperature_c;
    int humidity_pct;
} dht11_sample_t;

static uint8_t s_oled_buffer[OLED_H_RES * OLED_V_RES / 8];
static bool s_oled_ready;
static uint8_t s_oled_addr = OLED_I2C_ADDR_PRIMARY;

static sensor_type_t s_pressure_type;
static uint8_t s_pressure_addr;
static bmp180_calib_t s_bmp180;
static bmp280_calib_t s_bmp280;
static int32_t s_bmp280_t_fine;
static int s_page_index;
static int s_seq;
static led_strip_handle_t s_rgb_led;

static void board_rgb_off(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = RGB_LED_COUNT,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = RGB_RMT_RESOLUTION_HZ,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_rgb_led));
    ESP_ERROR_CHECK(led_strip_clear(s_rgb_led));
    ESP_LOGI(TAG, "Board RGB LED turned off on GPIO%d", RGB_LED_GPIO);
}

static uint16_t read_le16(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint16_t read_be16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static esp_err_t i2c_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_HOST, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_HOST, addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = {reg, value};
    return i2c_master_write_to_device(I2C_HOST, addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static esp_err_t oled_write_command(uint8_t command)
{
    uint8_t buffer[2] = {0x00, command};
    return i2c_master_write_to_device(I2C_HOST, s_oled_addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static esp_err_t oled_write_data(const uint8_t *data, size_t data_len)
{
    uint8_t tx[17];
    tx[0] = 0x40;

    while (data_len > 0) {
        size_t chunk = data_len > 16 ? 16 : data_len;
        memcpy(&tx[1], data, chunk);
        ESP_RETURN_ON_ERROR(
            i2c_master_write_to_device(I2C_HOST, s_oled_addr, tx, chunk + 1, pdMS_TO_TICKS(100)),
            TAG,
            "oled data write failed"
        );
        data += chunk;
        data_len -= chunk;
    }

    return ESP_OK;
}

static const uint8_t *font5x7(char c)
{
    static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

    switch (c) {
    case ' ': return (const uint8_t[5]){0x00, 0x00, 0x00, 0x00, 0x00};
    case '-': return (const uint8_t[5]){0x08, 0x08, 0x08, 0x08, 0x08};
    case '.': return (const uint8_t[5]){0x00, 0x60, 0x60, 0x00, 0x00};
    case ':': return (const uint8_t[5]){0x00, 0x36, 0x36, 0x00, 0x00};
    case '%': return (const uint8_t[5]){0x62, 0x64, 0x08, 0x13, 0x23};
    case '0': return (const uint8_t[5]){0x3E, 0x51, 0x49, 0x45, 0x3E};
    case '1': return (const uint8_t[5]){0x00, 0x42, 0x7F, 0x40, 0x00};
    case '2': return (const uint8_t[5]){0x42, 0x61, 0x51, 0x49, 0x46};
    case '3': return (const uint8_t[5]){0x21, 0x41, 0x45, 0x4B, 0x31};
    case '4': return (const uint8_t[5]){0x18, 0x14, 0x12, 0x7F, 0x10};
    case '5': return (const uint8_t[5]){0x27, 0x45, 0x45, 0x45, 0x39};
    case '6': return (const uint8_t[5]){0x3C, 0x4A, 0x49, 0x49, 0x30};
    case '7': return (const uint8_t[5]){0x01, 0x71, 0x09, 0x05, 0x03};
    case '8': return (const uint8_t[5]){0x36, 0x49, 0x49, 0x49, 0x36};
    case '9': return (const uint8_t[5]){0x06, 0x49, 0x49, 0x29, 0x1E};
    case 'A': return (const uint8_t[5]){0x7E, 0x11, 0x11, 0x11, 0x7E};
    case 'B': return (const uint8_t[5]){0x7F, 0x49, 0x49, 0x49, 0x36};
    case 'C': return (const uint8_t[5]){0x3E, 0x41, 0x41, 0x41, 0x22};
    case 'D': return (const uint8_t[5]){0x7F, 0x41, 0x41, 0x22, 0x1C};
    case 'E': return (const uint8_t[5]){0x7F, 0x49, 0x49, 0x49, 0x41};
    case 'G': return (const uint8_t[5]){0x3E, 0x41, 0x49, 0x49, 0x7A};
    case 'H': return (const uint8_t[5]){0x7F, 0x08, 0x08, 0x08, 0x7F};
    case 'I': return (const uint8_t[5]){0x00, 0x41, 0x7F, 0x41, 0x00};
    case 'K': return (const uint8_t[5]){0x7F, 0x08, 0x14, 0x22, 0x41};
    case 'L': return (const uint8_t[5]){0x7F, 0x40, 0x40, 0x40, 0x40};
    case 'M': return (const uint8_t[5]){0x7F, 0x02, 0x0C, 0x02, 0x7F};
    case 'N': return (const uint8_t[5]){0x7F, 0x06, 0x08, 0x30, 0x7F};
    case 'O': return (const uint8_t[5]){0x3E, 0x41, 0x41, 0x41, 0x3E};
    case 'P': return (const uint8_t[5]){0x7F, 0x09, 0x09, 0x09, 0x06};
    case 'Q': return (const uint8_t[5]){0x3E, 0x41, 0x51, 0x21, 0x5E};
    case 'R': return (const uint8_t[5]){0x7F, 0x09, 0x19, 0x29, 0x46};
    case 'S': return (const uint8_t[5]){0x46, 0x49, 0x49, 0x49, 0x31};
    case 'T': return (const uint8_t[5]){0x01, 0x01, 0x7F, 0x01, 0x01};
    case 'U': return (const uint8_t[5]){0x3F, 0x40, 0x40, 0x40, 0x3F};
    case 'V': return (const uint8_t[5]){0x1F, 0x20, 0x40, 0x20, 0x1F};
    case 'W': return (const uint8_t[5]){0x7F, 0x20, 0x18, 0x20, 0x7F};
    case 'Y': return (const uint8_t[5]){0x03, 0x04, 0x78, 0x04, 0x03};
    default: return blank;
    }
}

static void oled_clear_buffer(void)
{
    memset(s_oled_buffer, 0x00, sizeof(s_oled_buffer));
}

static void oled_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_H_RES || y < 0 || y >= OLED_V_RES) {
        return;
    }

    uint16_t index = (uint16_t)x + (uint16_t)(y / 8) * OLED_H_RES;
    uint8_t mask = (uint8_t)(1U << (y % 8));

    if (on) {
        s_oled_buffer[index] |= mask;
    } else {
        s_oled_buffer[index] &= (uint8_t)~mask;
    }
}

static void oled_draw_char(int x, int y, char c)
{
    const uint8_t *glyph = font5x7(c);

    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            bool on = (glyph[col] & (1 << row)) != 0;
            oled_set_pixel(x + col, y + row, on);
        }
    }
}

static void oled_draw_text(int x, int y, const char *text)
{
    while (*text != '\0') {
        oled_draw_char(x, y, *text++);
        x += 6;
    }
}

static esp_err_t oled_flush(void)
{
    for (int page = 0; page < OLED_V_RES / 8; page++) {
        ESP_RETURN_ON_ERROR(oled_write_command((uint8_t)(0xB0 + page)), TAG, "set page failed");
        ESP_RETURN_ON_ERROR(oled_write_command(0x00), TAG, "set low column failed");
        ESP_RETURN_ON_ERROR(oled_write_command(0x10), TAG, "set high column failed");
        ESP_RETURN_ON_ERROR(oled_write_data(&s_oled_buffer[OLED_H_RES * page], OLED_H_RES), TAG, "page write failed");
    }

    return ESP_OK;
}

static esp_err_t oled_panel_init(void)
{
    const uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ESP_RETURN_ON_ERROR(oled_write_command(init_cmds[i]), TAG, "init command failed");
    }

    return ESP_OK;
}

static void oled_render_text4(const char *line1, const char *line2, const char *line3, const char *line4)
{
    if (!s_oled_ready) {
        return;
    }

    oled_clear_buffer();
    oled_draw_text(0, 0, line1);
    oled_draw_text(0, 16, line2);
    oled_draw_text(0, 32, line3);
    oled_draw_text(0, 48, line4);
    ESP_ERROR_CHECK(oled_flush());
}

static const char *pressure_label(sensor_type_t type)
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

static esp_err_t bmp180_load_calibration(uint8_t addr)
{
    uint8_t raw[22];
    ESP_RETURN_ON_ERROR(i2c_read_reg(addr, 0xAA, raw, sizeof(raw)), TAG, "bmp180 calib read failed");

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

    ESP_RETURN_ON_ERROR(i2c_write_reg(addr, 0xF4, 0x2E), TAG, "bmp180 temp trigger failed");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(i2c_read_reg(addr, 0xF6, raw, 2), TAG, "bmp180 temp read failed");
    int32_t ut = (int32_t)read_be16(raw);

    ESP_RETURN_ON_ERROR(i2c_write_reg(addr, 0xF4, (uint8_t)(0x34 + (oss << 6))), TAG, "bmp180 press trigger failed");
    vTaskDelay(pdMS_TO_TICKS(8));
    ESP_RETURN_ON_ERROR(i2c_read_reg(addr, 0xF6, raw, 3), TAG, "bmp180 press read failed");
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
    sample->altitude_m = 44330.0f * (1.0f - powf(sample->pressure_pa / 101325.0f, 0.1903f));
    return ESP_OK;
}

static esp_err_t bmp280_load_calibration(uint8_t addr)
{
    uint8_t raw[24];
    ESP_RETURN_ON_ERROR(i2c_read_reg(addr, 0x88, raw, sizeof(raw)), TAG, "bmp280 calib read failed");

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
        ESP_RETURN_ON_ERROR(i2c_write_reg(addr, 0xF2, 0x01), TAG, "bme280 ctrl_hum write failed");
    }

    ESP_RETURN_ON_ERROR(i2c_write_reg(addr, 0xF5, 0x00), TAG, "bmp280 config write failed");
    ESP_RETURN_ON_ERROR(i2c_write_reg(addr, 0xF4, 0x27), TAG, "bmp280 ctrl_meas write failed");
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
    ESP_RETURN_ON_ERROR(i2c_read_reg(addr, 0xF7, raw, sizeof(raw)), TAG, "bmp280 sample read failed");

    int32_t adc_p = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_t = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);

    int32_t temp_x100 = bmp280_compensate_temp(adc_t);
    uint32_t pressure_q24_8 = bmp280_compensate_pressure(adc_p);

    sample->ready = true;
    sample->temperature_c = temp_x100 / 100.0f;
    sample->pressure_pa = pressure_q24_8 / 256.0f;
    sample->pressure_hpa = sample->pressure_pa / 100.0f;
    sample->altitude_m = 44330.0f * (1.0f - powf(sample->pressure_pa / 101325.0f, 0.1903f));
    return ESP_OK;
}

static esp_err_t pressure_sensor_detect(void)
{
    const uint8_t candidate_addrs[] = {0x76, 0x77};

    for (size_t i = 0; i < sizeof(candidate_addrs); i++) {
        uint8_t addr = candidate_addrs[i];
        if (i2c_probe(addr) != ESP_OK) {
            continue;
        }

        uint8_t chip_id = 0;
        if (i2c_read_reg(addr, 0xD0, &chip_id, 1) != ESP_OK) {
            continue;
        }

        if (chip_id == BMP180_CHIP_ID) {
            ESP_RETURN_ON_ERROR(bmp180_load_calibration(addr), TAG, "bmp180 calibration load failed");
            s_pressure_type = SENSOR_TYPE_BMP180;
            s_pressure_addr = addr;
            return ESP_OK;
        }

        if (chip_id == BMP280_CHIP_ID || chip_id == BME280_CHIP_ID) {
            sensor_type_t type = chip_id == BME280_CHIP_ID ? SENSOR_TYPE_BME280 : SENSOR_TYPE_BMP280;
            ESP_RETURN_ON_ERROR(bmp280_load_calibration(addr), TAG, "bmp280 calibration load failed");
            ESP_RETURN_ON_ERROR(bmp280_configure(addr, type), TAG, "bmp280 configure failed");
            s_pressure_type = type;
            s_pressure_addr = addr;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t pressure_sensor_read(pressure_sample_t *sample)
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

static void ds18b20_bus_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << DS18B20_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(DS18B20_GPIO, 1);
}

static inline void ds18b20_bus_low(void)
{
    gpio_set_level(DS18B20_GPIO, 0);
}

static inline void ds18b20_bus_release(void)
{
    gpio_set_level(DS18B20_GPIO, 1);
}

static bool ds18b20_reset_pulse(void)
{
    ds18b20_bus_low();
    esp_rom_delay_us(480);
    ds18b20_bus_release();
    esp_rom_delay_us(70);
    bool present = gpio_get_level(DS18B20_GPIO) == 0;
    esp_rom_delay_us(410);
    return present;
}

static void ds18b20_write_bit(int bit)
{
    ds18b20_bus_low();
    if (bit) {
        esp_rom_delay_us(6);
        ds18b20_bus_release();
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        ds18b20_bus_release();
        esp_rom_delay_us(10);
    }
}

static int ds18b20_read_bit(void)
{
    ds18b20_bus_low();
    esp_rom_delay_us(6);
    ds18b20_bus_release();
    esp_rom_delay_us(9);
    int bit = gpio_get_level(DS18B20_GPIO);
    esp_rom_delay_us(55);
    return bit;
}

static void ds18b20_write_byte(uint8_t value)
{
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(value & 0x01);
        value >>= 1;
    }
}

static uint8_t ds18b20_read_byte(void)
{
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        if (ds18b20_read_bit()) {
            value |= (uint8_t)(1U << i);
        }
    }
    return value;
}

static uint8_t ds18b20_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t inbyte = data[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }

    return crc;
}

static esp_err_t ds18b20_read_sample(ds18b20_sample_t *sample)
{
    uint8_t scratchpad[9];
    memset(sample, 0, sizeof(*sample));

    if (!ds18b20_reset_pulse()) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(DS18B20_CMD_CONVERT_T);
    vTaskDelay(pdMS_TO_TICKS(800));

    if (!ds18b20_reset_pulse()) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(DS18B20_CMD_READ_PAD);

    for (int i = 0; i < 9; i++) {
        scratchpad[i] = ds18b20_read_byte();
    }

    if (ds18b20_crc8(scratchpad, 8) != scratchpad[8]) {
        return ESP_ERR_INVALID_CRC;
    }

    int16_t raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
    sample->ready = true;
    sample->temperature_c = raw / 16.0f;
    return ESP_OK;
}

static void dht11_bus_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << DHT11_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(DHT11_GPIO, 1);
}

static esp_err_t dht11_wait_level(int level, uint32_t timeout_us)
{
    while (timeout_us--) {
        if (gpio_get_level(DHT11_GPIO) == level) {
            return ESP_OK;
        }
        esp_rom_delay_us(1);
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t dht11_read_sample(dht11_sample_t *sample)
{
    uint8_t data[5] = {0};
    memset(sample, 0, sizeof(*sample));

    gpio_set_level(DHT11_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DHT11_GPIO, 1);
    esp_rom_delay_us(30);

    if (dht11_wait_level(0, 100) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (dht11_wait_level(1, 100) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (dht11_wait_level(0, 100) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < 40; i++) {
        if (dht11_wait_level(1, 100) != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        esp_rom_delay_us(35);
        int bit = gpio_get_level(DHT11_GPIO);
        if (dht11_wait_level(0, 100) != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        data[i / 8] = (uint8_t)((data[i / 8] << 1) | (bit ? 1 : 0));
    }

    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    sample->ready = true;
    sample->humidity_pct = data[0];
    sample->temperature_c = data[2];
    return ESP_OK;
}

static void log_samples(const pressure_sample_t *pressure, const ds18b20_sample_t *ds18b20, const dht11_sample_t *dht11)
{
    s_seq++;
    ESP_LOGI(
        TAG,
        "{\"seq\":%d,\"press_ready\":%d,\"press_chip\":\"%s\",\"press_hpa\":%.2f,\"press_temp_c\":%.2f,"
        "\"ds18_ready\":%d,\"ds18_temp_c\":%.2f,\"dht11_ready\":%d,\"dht11_temp_c\":%d,\"dht11_humi\":%d}",
        s_seq,
        pressure->ready ? 1 : 0,
        pressure_label(s_pressure_type),
        pressure->pressure_hpa,
        pressure->temperature_c,
        ds18b20->ready ? 1 : 0,
        ds18b20->temperature_c,
        dht11->ready ? 1 : 0,
        dht11->temperature_c,
        dht11->humidity_pct
    );
}

static void render_pressure_page(const pressure_sample_t *sample)
{
    char line2[24];
    char line3[24];
    char line4[24];

    if (!sample->ready) {
        oled_render_text4("PAGE1 PRESS", "NO SENSOR", "CHECK I2C", "GPIO5 GPIO6");
        return;
    }

    snprintf(line2, sizeof(line2), "P:%4.2fHPA", sample->pressure_hpa);
    snprintf(line3, sizeof(line3), "T:%2.2fC", sample->temperature_c);
    snprintf(line4, sizeof(line4), "ALT:%3.1fM", sample->altitude_m);
    oled_render_text4(pressure_label(s_pressure_type), line2, line3, line4);
}

static void render_ds18_page(const ds18b20_sample_t *sample)
{
    char line2[24];

    if (!sample->ready) {
        oled_render_text4("PAGE2 DS18", "NO SENSOR", "GPIO4", "PULLUP 4K7");
        return;
    }

    snprintf(line2, sizeof(line2), "TEMP:%2.2fC", sample->temperature_c);
    oled_render_text4("DS18B20", line2, "GPIO4", "PAGE2");
}

static void render_dht11_page(const dht11_sample_t *sample)
{
    char line2[24];
    char line3[24];

    if (!sample->ready) {
        oled_render_text4("PAGE3 DHT11", "NO SENSOR", "GPIO7", "PULLUP 10K");
        return;
    }

    snprintf(line2, sizeof(line2), "TEMP:%dC", sample->temperature_c);
    snprintf(line3, sizeof(line3), "HUMI:%d%%", sample->humidity_pct);
    oled_render_text4("DHT11", line2, line3, "PAGE3");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Start 3 sensor pages demo");
    ESP_LOGI(TAG, "I2C: SDA=GPIO%d SCL=GPIO%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    ESP_LOGI(TAG, "DS18B20 on GPIO%d, DHT11 on GPIO%d", DS18B20_GPIO, DHT11_GPIO);
    board_rgb_off();

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_CLOCK_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST, I2C_MODE_MASTER, 0, 0, 0));

    if (i2c_probe(OLED_I2C_ADDR_PRIMARY) == ESP_OK) {
        s_oled_addr = OLED_I2C_ADDR_PRIMARY;
        s_oled_ready = true;
    } else if (i2c_probe(OLED_I2C_ADDR_SECONDARY) == ESP_OK) {
        s_oled_addr = OLED_I2C_ADDR_SECONDARY;
        s_oled_ready = true;
    }

    if (s_oled_ready) {
        ESP_LOGI(TAG, "OLED detected at 0x%02X", s_oled_addr);
        ESP_ERROR_CHECK(oled_panel_init());
        oled_render_text4("3 SENSOR DEMO", "INIT", "WAIT DATA", "");
    } else {
        ESP_LOGW(TAG, "OLED not found at 0x3C/0x3D");
    }

    if (pressure_sensor_detect() == ESP_OK) {
        ESP_LOGI(TAG, "Pressure sensor detected: %s at 0x%02X", pressure_label(s_pressure_type), s_pressure_addr);
    } else {
        ESP_LOGW(TAG, "Pressure sensor not found");
    }

    ds18b20_bus_init();
    dht11_bus_init();

    while (1) {
        pressure_sample_t pressure = {0};
        ds18b20_sample_t ds18b20 = {0};
        dht11_sample_t dht11 = {0};

        if (s_pressure_type != SENSOR_TYPE_NONE) {
            esp_err_t ret = pressure_sensor_read(&pressure);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Pressure read failed: %s", esp_err_to_name(ret));
            }
        }

        esp_err_t ds_ret = ds18b20_read_sample(&ds18b20);
        if (ds_ret != ESP_OK) {
            ESP_LOGW(TAG, "DS18B20 read failed: %s", esp_err_to_name(ds_ret));
        }

        esp_err_t dht_ret = dht11_read_sample(&dht11);
        if (dht_ret != ESP_OK) {
            ESP_LOGW(TAG, "DHT11 read failed: %s", esp_err_to_name(dht_ret));
        }

        log_samples(&pressure, &ds18b20, &dht11);

        switch (s_page_index % 3) {
        case 0:
            render_pressure_page(&pressure);
            break;
        case 1:
            render_ds18_page(&ds18b20);
            break;
        default:
            render_dht11_page(&dht11);
            break;
        }

        s_page_index++;
        vTaskDelay(pdMS_TO_TICKS(PAGE_REFRESH_MS));
    }
}
