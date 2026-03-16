#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t oled_display_init(void);
bool oled_display_is_ready(void);
void oled_display_render_text4(const char *line1, const char *line2, const char *line3, const char *line4);
void oled_display_render_sensor_page(
    const char *page_text,
    const char *wifi_text,
    const char *server_text,
    const char *line2,
    const char *line3,
    const char *line4
);

#endif
