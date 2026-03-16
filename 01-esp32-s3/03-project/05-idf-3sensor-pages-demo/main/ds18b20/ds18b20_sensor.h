#ifndef DS18B20_SENSOR_H
#define DS18B20_SENSOR_H

#include "app/app_types.h"
#include "driver/gpio.h"
#include "esp_err.h"

void ds18b20_sensor_init(gpio_num_t gpio);
esp_err_t ds18b20_sensor_read(ds18b20_sample_t *sample);

#endif
