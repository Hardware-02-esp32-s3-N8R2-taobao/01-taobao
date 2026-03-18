#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t gpio_num;
    uint32_t active_level;
} led_driver_config_t;

esp_err_t led_driver_init(const led_driver_config_t *config);
esp_err_t led_driver_set(bool on);
