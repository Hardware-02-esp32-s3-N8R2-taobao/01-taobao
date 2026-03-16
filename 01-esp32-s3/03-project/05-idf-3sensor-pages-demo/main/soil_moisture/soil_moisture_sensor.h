#ifndef SOIL_MOISTURE_SENSOR_H
#define SOIL_MOISTURE_SENSOR_H

#include <stdbool.h>

#include "app/app_types.h"
#include "esp_err.h"

esp_err_t soil_moisture_sensor_init(void);
bool soil_moisture_sensor_is_ready(void);
esp_err_t soil_moisture_sensor_read(soil_moisture_sample_t *sample);

#endif
