#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    uint32_t pixel_clock_hz;
    uint8_t width;
    uint8_t height;
    uint8_t column_offset;
    uint8_t display_offset;
    uint8_t primary_addr;
    uint8_t secondary_addr;
    bool rotate_180;
} oled_ssd1306_config_t;

esp_err_t oled_ssd1306_init(const oled_ssd1306_config_t *config);
bool oled_ssd1306_is_ready(void);
uint8_t oled_ssd1306_get_address(void);
void oled_ssd1306_clear(void);
esp_err_t oled_ssd1306_set_display_enabled(bool enabled);
void oled_ssd1306_draw_text(int x, int y, const char *text);
void oled_ssd1306_draw_text_scaled(int x, int y, const char *text, uint8_t scale);
void oled_ssd1306_draw_text_scaled_bold(int x, int y, const char *text, uint8_t scale);
void oled_ssd1306_draw_text_emphasis(int x, int y, const char *text);
void oled_ssd1306_draw_rect(int x, int y, int w, int h, bool on);
void oled_ssd1306_fill_rect(int x, int y, int w, int h, bool on);
esp_err_t oled_ssd1306_present(void);
