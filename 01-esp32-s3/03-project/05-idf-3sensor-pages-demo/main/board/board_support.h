#ifndef BOARD_SUPPORT_H
#define BOARD_SUPPORT_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t board_rgb_init(void);
esp_err_t board_rgb_set(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t board_rgb_off(void);

#endif
