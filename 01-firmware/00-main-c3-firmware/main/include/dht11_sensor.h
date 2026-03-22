#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    bool ready;
    float temperature_c;
    float humidity_pct;
} dht11_sample_t;

void dht11_sensor_init(gpio_num_t gpio);
esp_err_t dht11_sensor_read(dht11_sample_t *sample);
