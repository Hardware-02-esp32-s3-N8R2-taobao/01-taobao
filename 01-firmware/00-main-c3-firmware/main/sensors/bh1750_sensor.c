#include "bh1750_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_check.h"

#include "app_config.h"
#include "sensor_bus.h"

#define BH1750_ONE_TIME_HIGH_RES_MODE 0x20

static uint8_t s_addr = 0;

static esp_err_t probe_addr(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(sensor_bus_i2c_port(), cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t bh1750_sensor_init(void)
{
    ESP_RETURN_ON_ERROR(sensor_bus_init(), "bh1750", "sensor bus init failed");
    if (probe_addr(APP_BH1750_ADDR_PRIMARY) == ESP_OK) {
        s_addr = APP_BH1750_ADDR_PRIMARY;
        return ESP_OK;
    }
    if (probe_addr(APP_BH1750_ADDR_SECONDARY) == ESP_OK) {
        s_addr = APP_BH1750_ADDR_SECONDARY;
        return ESP_OK;
    }
    s_addr = 0;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bh1750_sensor_read(bh1750_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_addr == 0) {
        esp_err_t init_ret = bh1750_sensor_init();
        if (init_ret != ESP_OK) {
            sample->ready = false;
            return init_ret;
        }
    }

    uint8_t cmd = BH1750_ONE_TIME_HIGH_RES_MODE;
    ESP_RETURN_ON_ERROR(
        i2c_master_write_to_device(sensor_bus_i2c_port(), s_addr, &cmd, 1, pdMS_TO_TICKS(100)),
        "bh1750",
        "command write failed"
    );
    vTaskDelay(pdMS_TO_TICKS(180));

    uint8_t data[2] = {0};
    ESP_RETURN_ON_ERROR(
        i2c_master_read_from_device(sensor_bus_i2c_port(), s_addr, data, sizeof(data), pdMS_TO_TICKS(100)),
        "bh1750",
        "read failed"
    );

    uint16_t raw = (uint16_t)((data[0] << 8) | data[1]);
    sample->ready = true;
    sample->address = s_addr;
    sample->illuminance_lux = (float)raw / 1.2f;
    return ESP_OK;
}
