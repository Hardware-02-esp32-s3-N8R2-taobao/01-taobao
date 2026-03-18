#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"

#define APP_BLINK_PERIOD_MS         500

#define APP_LED_GPIO                GPIO_NUM_8
#define APP_LED_ACTIVE_LEVEL        0

#define APP_I2C_HOST                I2C_NUM_0
#define APP_OLED_I2C_SDA_GPIO       GPIO_NUM_4
#define APP_OLED_I2C_SCL_GPIO       GPIO_NUM_5
#define APP_OLED_I2C_ADDR_PRIMARY   0x3C
#define APP_OLED_I2C_ADDR_SECONDARY 0x3D
#define APP_OLED_PIXEL_CLOCK_HZ     (400 * 1000)
