#ifndef PRESSURE_SENSOR_H
#define PRESSURE_SENSOR_H

#include "app/app_types.h"
#include "esp_err.h"

esp_err_t pressure_sensor_init(void);
esp_err_t pressure_sensor_read(pressure_sample_t *sample);
sensor_type_t pressure_sensor_type(void);
const char *pressure_sensor_label(sensor_type_t type);

#endif
