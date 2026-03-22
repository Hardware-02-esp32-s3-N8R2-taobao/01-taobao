#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    bool ready;
    int raw_value;
    float percent;
} analog_sensor_sample_t;

esp_err_t analog_sensor_init(void);
esp_err_t analog_sensor_read_soil(analog_sensor_sample_t *sample);
esp_err_t analog_sensor_read_rain(analog_sensor_sample_t *sample);
