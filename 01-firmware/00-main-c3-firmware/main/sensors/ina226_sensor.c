#include "ina226_sensor.h"

#include <string.h>

#include "driver/i2c.h"
#include "esp_check.h"

#include "app_config.h"
#include "sensor_bus.h"

#define TAG "ina226"

#define INA226_REG_CONFIG       0x00
#define INA226_REG_SHUNT_VOLT   0x01
#define INA226_REG_BUS_VOLT     0x02
#define INA226_REG_POWER        0x03
#define INA226_REG_CURRENT      0x04
#define INA226_REG_CALIBRATION  0x05

#define INA226_CONFIG_DEFAULT   0x4527
#define INA226_CALIBRATION      0x1000
#define INA226_CURRENT_LSB_A    0.001f
#define INA226_POWER_LSB_W      (25.0f * INA226_CURRENT_LSB_A)

static esp_err_t write_register_u16(uint8_t addr, uint8_t reg, uint16_t value)
{
    uint8_t data[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_write_to_device(sensor_bus_i2c_port(), addr, data, sizeof(data), pdMS_TO_TICKS(100)),
        TAG,
        "i2c write failed"
    );
    return ESP_OK;
}

static esp_err_t read_register_u16(uint8_t addr, uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = {0};
    ESP_RETURN_ON_ERROR(
        i2c_master_write_read_device(sensor_bus_i2c_port(), addr, &reg, 1, data, sizeof(data), pdMS_TO_TICKS(100)),
        TAG,
        "i2c read failed"
    );
    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return ESP_OK;
}

esp_err_t ina226_sensor_read(ina226_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));

    if (!sensor_bus_is_ready()) {
        ESP_RETURN_ON_ERROR(sensor_bus_init(), TAG, "i2c init failed");
    }

    ESP_RETURN_ON_ERROR(write_register_u16(APP_INA226_ADDR, INA226_REG_CONFIG, INA226_CONFIG_DEFAULT), TAG, "config failed");
    ESP_RETURN_ON_ERROR(write_register_u16(APP_INA226_ADDR, INA226_REG_CALIBRATION, INA226_CALIBRATION), TAG, "calibration failed");

    uint16_t raw_bus_voltage = 0;
    uint16_t raw_current = 0;
    uint16_t raw_power = 0;
    ESP_RETURN_ON_ERROR(read_register_u16(APP_INA226_ADDR, INA226_REG_BUS_VOLT, &raw_bus_voltage), TAG, "read bus voltage failed");
    ESP_RETURN_ON_ERROR(read_register_u16(APP_INA226_ADDR, INA226_REG_CURRENT, &raw_current), TAG, "read current failed");
    ESP_RETURN_ON_ERROR(read_register_u16(APP_INA226_ADDR, INA226_REG_POWER, &raw_power), TAG, "read power failed");

    sample->ready = true;
    sample->address = APP_INA226_ADDR;
    sample->raw_bus_voltage = raw_bus_voltage;
    sample->raw_current = (int16_t)raw_current;
    sample->raw_power = raw_power;
    sample->bus_voltage_v = (float)raw_bus_voltage * 0.00125f;
    sample->current_ma = ((int16_t)raw_current) * INA226_CURRENT_LSB_A * 1000.0f;
    sample->power_mw = (float)raw_power * INA226_POWER_LSB_W * 1000.0f;

    return ESP_OK;
}
