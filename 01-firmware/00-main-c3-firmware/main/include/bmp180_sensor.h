#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define BMP180_CHIP_ID 0x55
#define BMP280_CHIP_ID 0x58

typedef struct {
    bool ready;
    uint8_t address;
    uint8_t chip_id;
    float temperature_c;
    float pressure_hpa;
} bmp180_sample_t;

esp_err_t bmp180_sensor_init(void);
esp_err_t bmp180_sensor_read(bmp180_sample_t *sample);
esp_err_t bmp180_sensor_build_debug_json(char *buffer, size_t buffer_size);
