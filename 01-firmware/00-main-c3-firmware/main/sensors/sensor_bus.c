#include "sensor_bus.h"

#include <string.h>

#include "driver/i2c.h"
#include "esp_check.h"

#include "app_config.h"
#include "device_profile.h"

#define TAG "sensor_bus"

static sensor_i2c_bus_config_t s_config;
static bool s_ready = false;

static void resolve_bus_config(sensor_i2c_bus_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->i2c_port = APP_I2C_PORT;
    config->clock_hz = APP_I2C_CLOCK_HZ;

    if (device_profile_hardware_variant() == DEVICE_HW_VARIANT_OLED_SCREEN) {
        config->sda_gpio = APP_I2C_SDA_OLED_SCREEN;
        config->scl_gpio = APP_I2C_SCL_OLED_SCREEN;
    } else {
        config->sda_gpio = APP_I2C_SDA_SUPERMINI;
        config->scl_gpio = APP_I2C_SCL_SUPERMINI;
    }
}

esp_err_t sensor_bus_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    resolve_bus_config(&s_config);

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = s_config.sda_gpio,
        .scl_io_num = s_config.scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = s_config.clock_hz,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(s_config.i2c_port, &cfg), TAG, "i2c param config failed");

    esp_err_t install_ret = i2c_driver_install(s_config.i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (install_ret != ESP_OK && install_ret != ESP_ERR_INVALID_STATE) {
        return install_ret;
    }

    s_ready = true;
    return ESP_OK;
}

bool sensor_bus_is_ready(void)
{
    return s_ready;
}

i2c_port_t sensor_bus_i2c_port(void)
{
    return s_config.i2c_port;
}

gpio_num_t sensor_bus_i2c_sda_gpio(void)
{
    return s_config.sda_gpio;
}

gpio_num_t sensor_bus_i2c_scl_gpio(void)
{
    return s_config.scl_gpio;
}
