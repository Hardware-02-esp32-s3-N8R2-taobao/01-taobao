#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    uint32_t clock_hz;
} sensor_i2c_bus_config_t;

esp_err_t sensor_bus_init(void);
bool sensor_bus_is_ready(void);
i2c_port_t sensor_bus_i2c_port(void);
gpio_num_t sensor_bus_i2c_sda_gpio(void);
gpio_num_t sensor_bus_i2c_scl_gpio(void);
