#include "i2c_bus/i2c_bus.h"

#include "driver/gpio.h"
#include "driver/i2c.h"

#include "app/app_config.h"

esp_err_t i2c_bus_init(void)
{
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_CLOCK_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
    return i2c_driver_install(I2C_HOST, I2C_MODE_MASTER, 0, 0, 0);
}

esp_err_t i2c_bus_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_HOST, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_bus_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_HOST, addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

esp_err_t i2c_bus_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = {reg, value};
    return i2c_master_write_to_device(I2C_HOST, addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

esp_err_t i2c_bus_write_byte(uint8_t addr, uint8_t value)
{
    return i2c_master_write_to_device(I2C_HOST, addr, &value, 1, pdMS_TO_TICKS(100));
}

esp_err_t i2c_bus_read_bytes(uint8_t addr, uint8_t *data, size_t len)
{
    return i2c_master_read_from_device(I2C_HOST, addr, data, len, pdMS_TO_TICKS(100));
}
