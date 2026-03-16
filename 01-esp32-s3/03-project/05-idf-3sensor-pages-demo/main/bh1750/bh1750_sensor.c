#include "bh1750/bh1750_sensor.h"

#include <string.h>

#include "esp_check.h"

#include "app/app_config.h"
#include "i2c_bus/i2c_bus.h"

#define BH1750_ADDR_LOW 0x23
#define BH1750_ADDR_HIGH 0x5C
#define BH1750_CMD_POWER_ON 0x01
#define BH1750_CMD_RESET 0x07
#define BH1750_CMD_CONT_HRES 0x10

static uint8_t s_bh1750_addr;

esp_err_t bh1750_sensor_init(void)
{
    const uint8_t candidate_addrs[] = {BH1750_ADDR_LOW, BH1750_ADDR_HIGH};

    s_bh1750_addr = 0;

    for (size_t i = 0; i < sizeof(candidate_addrs); i++) {
        uint8_t addr = candidate_addrs[i];
        if (i2c_bus_probe(addr) != ESP_OK) {
            continue;
        }

        ESP_RETURN_ON_ERROR(i2c_bus_write_byte(addr, BH1750_CMD_POWER_ON), APP_TAG, "bh1750 power on failed");
        ESP_RETURN_ON_ERROR(i2c_bus_write_byte(addr, BH1750_CMD_RESET), APP_TAG, "bh1750 reset failed");
        ESP_RETURN_ON_ERROR(i2c_bus_write_byte(addr, BH1750_CMD_CONT_HRES), APP_TAG, "bh1750 mode set failed");
        s_bh1750_addr = addr;
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

bool bh1750_sensor_is_ready(void)
{
    return s_bh1750_addr != 0;
}

uint8_t bh1750_sensor_address(void)
{
    return s_bh1750_addr;
}

esp_err_t bh1750_sensor_read(bh1750_sample_t *sample)
{
    uint8_t raw[2];
    memset(sample, 0, sizeof(*sample));

    if (s_bh1750_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(i2c_bus_read_bytes(s_bh1750_addr, raw, sizeof(raw)), APP_TAG, "bh1750 read failed");

    uint16_t level = ((uint16_t)raw[0] << 8) | raw[1];
    sample->ready = true;
    sample->lux = level / 1.2f;
    return ESP_OK;
}
