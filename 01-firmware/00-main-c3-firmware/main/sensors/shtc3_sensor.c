#include "shtc3_sensor.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_check.h"

#include "app_config.h"
#include "sensor_bus.h"

#define SHT3X_ADDR_PRIMARY                      0x44
#define SHT3X_ADDR_SECONDARY                    0x45
#define SHT3X_MEASURE_HIGH_REPEATABILITY        0x2400

#define SHTC3_WAKEUP_CMD                        0x3517
#define SHTC3_SLEEP_CMD                         0xB098
#define SHTC3_MEASURE_NORMAL_T_FIRST_NO_STRETCH 0x7866

typedef enum {
    SENSOR_KIND_UNKNOWN = 0,
    SENSOR_KIND_SHTC3,
    SENSOR_KIND_SHT3X,
} sensor_kind_t;

static uint8_t s_addr = 0;
static sensor_kind_t s_kind = SENSOR_KIND_UNKNOWN;
static bool s_bus_ready = false;

static esp_err_t write_cmd(uint8_t addr, uint16_t cmd)
{
    uint8_t payload[2] = {
        (uint8_t)(cmd >> 8),
        (uint8_t)(cmd & 0xFF),
    };
    return i2c_master_write_to_device(sensor_bus_i2c_port(), addr, payload, sizeof(payload), pdMS_TO_TICKS(100));
}

static esp_err_t read_bytes(uint8_t addr, uint8_t *data, size_t len)
{
    return i2c_master_read_from_device(sensor_bus_i2c_port(), addr, data, len, pdMS_TO_TICKS(100));
}

static uint8_t crc8_sht(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t parse_measurement(
    const uint8_t data[6],
    bool temperature_first,
    float *temperature_c,
    float *humidity_pct
)
{
    const uint8_t *first = &data[0];
    const uint8_t *second = &data[3];

    if (crc8_sht(first, 2) != first[2] || crc8_sht(second, 2) != second[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t first_raw = (uint16_t)((first[0] << 8) | first[1]);
    uint16_t second_raw = (uint16_t)((second[0] << 8) | second[1]);
    uint16_t raw_temp = temperature_first ? first_raw : second_raw;
    uint16_t raw_humidity = temperature_first ? second_raw : first_raw;

    *temperature_c = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    *humidity_pct = 100.0f * ((float)raw_humidity / 65535.0f);
    return ESP_OK;
}

static esp_err_t try_read_shtc3(uint8_t addr, shtc3_sample_t *sample)
{
    esp_err_t wake_ret = write_cmd(addr, SHTC3_WAKEUP_CMD);
    if (wake_ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_RETURN_ON_ERROR(write_cmd(addr, SHTC3_MEASURE_NORMAL_T_FIRST_NO_STRETCH), "shtc3", "shtc3 measure failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t data[6] = {0};
    ESP_RETURN_ON_ERROR(read_bytes(addr, data, sizeof(data)), "shtc3", "shtc3 read failed");

    esp_err_t parse_ret = parse_measurement(data, true, &sample->temperature_c, &sample->humidity_pct);
    write_cmd(addr, SHTC3_SLEEP_CMD);
    if (parse_ret != ESP_OK) {
        return parse_ret;
    }

    sample->address = addr;
    sample->ready = true;
    return ESP_OK;
}

static esp_err_t try_read_sht3x(uint8_t addr, shtc3_sample_t *sample)
{
    ESP_RETURN_ON_ERROR(write_cmd(addr, SHT3X_MEASURE_HIGH_REPEATABILITY), "shtc3", "sht3x measure failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t data[6] = {0};
    ESP_RETURN_ON_ERROR(read_bytes(addr, data, sizeof(data)), "shtc3", "sht3x read failed");

    ESP_RETURN_ON_ERROR(parse_measurement(data, true, &sample->temperature_c, &sample->humidity_pct), "shtc3", "sht3x crc failed");
    sample->address = addr;
    sample->ready = true;
    return ESP_OK;
}

esp_err_t shtc3_sensor_init(void)
{
    if (s_bus_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(sensor_bus_init(), "shtc3", "sensor bus init failed");
    s_bus_ready = true;
    return ESP_OK;
}

esp_err_t shtc3_sensor_read(shtc3_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));
    sample->address = APP_SHTC3_ADDR;

    ESP_RETURN_ON_ERROR(shtc3_sensor_init(), "shtc3", "sensor init failed");

    if (s_addr != 0 && s_kind != SENSOR_KIND_UNKNOWN) {
        for (int retry = 0; retry < 3; retry++) {
            esp_err_t ret = (s_kind == SENSOR_KIND_SHTC3)
                ? try_read_shtc3(s_addr, sample)
                : try_read_sht3x(s_addr, sample);
            if (ret == ESP_OK) {
                return ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    struct candidate_t {
        uint8_t addr;
        sensor_kind_t kind;
    };

    const struct candidate_t candidates[] = {
        { APP_SHTC3_ADDR, SENSOR_KIND_SHTC3 },
        { SHT3X_ADDR_PRIMARY, SENSOR_KIND_SHT3X },
        { SHT3X_ADDR_SECONDARY, SENSOR_KIND_SHT3X },
    };

    esp_err_t last_error = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        uint8_t addr = candidates[i].addr;
        sensor_kind_t kind = candidates[i].kind;
        for (int retry = 0; retry < 2; retry++) {
            esp_err_t ret = (kind == SENSOR_KIND_SHTC3)
                ? try_read_shtc3(addr, sample)
                : try_read_sht3x(addr, sample);
            if (ret == ESP_OK) {
                s_addr = addr;
                s_kind = kind;
                return ESP_OK;
            }
            last_error = ret;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    sample->address = (s_addr != 0) ? s_addr : APP_SHTC3_ADDR;
    return last_error;
}
