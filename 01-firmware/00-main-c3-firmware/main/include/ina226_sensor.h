#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ready;
    uint8_t address;
    float bus_voltage_v;
    float current_ma;
    float power_mw;
    uint16_t raw_bus_voltage;
    int16_t raw_current;
    uint16_t raw_power;
} ina226_sample_t;

esp_err_t ina226_sensor_read(ina226_sample_t *sample);
