#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ready;
    uint8_t address;
    float voltage_v;
    float percent;
    uint16_t raw_vcell;
    uint16_t raw_soc;
} max17043_sample_t;

esp_err_t max17043_sensor_read(max17043_sample_t *sample);
