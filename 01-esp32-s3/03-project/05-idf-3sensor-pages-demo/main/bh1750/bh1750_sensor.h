#ifndef BH1750_SENSOR_H
#define BH1750_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "app/app_types.h"
#include "esp_err.h"

esp_err_t bh1750_sensor_init(void);
bool bh1750_sensor_is_ready(void);
uint8_t bh1750_sensor_address(void);
esp_err_t bh1750_sensor_read(bh1750_sample_t *sample);

#endif
