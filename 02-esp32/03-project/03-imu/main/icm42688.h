#ifndef ICM42688_H
#define ICM42688_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t host;
    gpio_num_t sclk_io;
    gpio_num_t mosi_io;
    gpio_num_t miso_io;
    gpio_num_t cs_io;
    int clock_hz;
} icm42688_config_t;

typedef struct {
    spi_device_handle_t spi;
    icm42688_config_t config;
} icm42688_t;

typedef struct {
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float temperature_c;
} icm42688_sample_t;

esp_err_t icm42688_init(icm42688_t *dev, const icm42688_config_t *config);
esp_err_t icm42688_read_who_am_i(icm42688_t *dev, uint8_t *who_am_i);
esp_err_t icm42688_read_sample(icm42688_t *dev, icm42688_sample_t *sample);

#endif
