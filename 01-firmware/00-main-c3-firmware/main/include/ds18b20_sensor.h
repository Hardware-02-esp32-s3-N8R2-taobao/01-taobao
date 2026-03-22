#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    bool ready;
    float temperature_c;
} ds18b20_sample_t;

void ds18b20_sensor_init(gpio_num_t gpio);
esp_err_t ds18b20_sensor_read(ds18b20_sample_t *sample);
