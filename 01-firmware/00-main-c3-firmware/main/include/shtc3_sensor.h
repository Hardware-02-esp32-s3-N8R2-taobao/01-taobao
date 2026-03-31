#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ready;
    uint8_t address;
    float temperature_c;
    float humidity_pct;
} shtc3_sample_t;

esp_err_t shtc3_sensor_init(void);
esp_err_t shtc3_sensor_read(shtc3_sample_t *sample);
