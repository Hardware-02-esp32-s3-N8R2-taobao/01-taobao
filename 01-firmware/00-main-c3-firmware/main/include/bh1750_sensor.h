#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ready;
    uint8_t address;
    float illuminance_lux;
} bh1750_sample_t;

esp_err_t bh1750_sensor_init(void);
esp_err_t bh1750_sensor_read(bh1750_sample_t *sample);
