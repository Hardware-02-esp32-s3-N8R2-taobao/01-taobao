#ifndef RAIN_SENSOR_H
#define RAIN_SENSOR_H

#include <stdbool.h>

#include "app/app_types.h"
#include "esp_err.h"

esp_err_t rain_sensor_init(void);
bool rain_sensor_is_ready(void);
esp_err_t rain_sensor_read(rain_sensor_sample_t *sample);

#endif
