#ifndef DHT11_SENSOR_H
#define DHT11_SENSOR_H

#include "app/app_types.h"
#include "driver/gpio.h"
#include "esp_err.h"

void dht11_sensor_init(gpio_num_t gpio);
esp_err_t dht11_sensor_read(dht11_sample_t *sample);

#endif
