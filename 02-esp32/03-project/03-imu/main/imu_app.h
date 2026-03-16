#ifndef IMU_APP_H
#define IMU_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t imu_app_init(void);
esp_err_t imu_app_print_who_am_i(void);
esp_err_t imu_app_print_one_sample(void);
esp_err_t imu_app_start_streaming(uint32_t sample_count, uint32_t period_ms);
void imu_app_stop_streaming(void);
bool imu_app_is_streaming(void);

#endif
