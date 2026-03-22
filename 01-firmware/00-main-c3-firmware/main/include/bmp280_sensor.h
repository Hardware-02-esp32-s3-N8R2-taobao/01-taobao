#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ready;
    uint8_t address;
    uint8_t chip_id;
    bool has_humidity;
    float temperature_c;
    float pressure_hpa;
    float humidity_pct;
} bmp280_sample_t;

esp_err_t bmp280_sensor_init(void);
esp_err_t bmp280_sensor_read(bmp280_sample_t *sample);
