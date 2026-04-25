#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    STATUS_LED_MODE_NORMAL = 0,
    STATUS_LED_MODE_LOW_POWER,
    STATUS_LED_MODE_MAINTENANCE,
} status_led_mode_t;

esp_err_t status_led_init(void);
void status_led_blink_publish(void);
void status_led_set_mode(status_led_mode_t mode);
void status_led_set_provisioning(bool enabled);
