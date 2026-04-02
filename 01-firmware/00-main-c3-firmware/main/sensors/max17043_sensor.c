#include "max17043_sensor.h"

#include <string.h>

#include "driver/i2c.h"
#include "esp_check.h"

#include "app_config.h"
#include "sensor_bus.h"

#define TAG "max17043"

#define MAX17043_REG_VCELL 0x02
#define MAX17043_REG_SOC   0x04

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

esp_err_t max17043_sensor_read(max17043_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));

    if (!sensor_bus_is_ready()) {
        ESP_RETURN_ON_ERROR(sensor_bus_init(), TAG, "i2c init failed");
    }

    uint16_t raw_vcell = 0;
    uint16_t raw_soc = 0;
    ESP_RETURN_ON_ERROR(read_register_u16(APP_MAX17043_ADDR, MAX17043_REG_VCELL, &raw_vcell), TAG, "read vcell failed");
    ESP_RETURN_ON_ERROR(read_register_u16(APP_MAX17043_ADDR, MAX17043_REG_SOC, &raw_soc), TAG, "read soc failed");

    sample->ready = true;
    sample->address = APP_MAX17043_ADDR;
    sample->raw_vcell = raw_vcell;
    sample->raw_soc = raw_soc;

    sample->voltage_v = ((float)(raw_vcell >> 4)) * 1.25f / 1000.0f;
    sample->percent = ((float)(raw_soc >> 8)) + ((float)(raw_soc & 0xFF) / 256.0f);
    if (sample->percent < 0.0f) {
        sample->percent = 0.0f;
    }
    if (sample->percent > 100.0f) {
        sample->percent = 100.0f;
    }

    return ESP_OK;
}
