#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    bool ready;
    int raw_value;
    float percent;
} analog_sensor_sample_t;

typedef struct {
    bool ready;
    int raw_value;
    float voltage_v;  // 电池实际电压（V），已还原分压
    float percent;    // 电量百分比 0~100%（基于 3.0V~4.2V 锂电）
} battery_voltage_sample_t;

esp_err_t analog_sensor_init(void);
esp_err_t analog_sensor_read_soil(analog_sensor_sample_t *sample);
esp_err_t analog_sensor_read_battery(battery_voltage_sample_t *sample);
