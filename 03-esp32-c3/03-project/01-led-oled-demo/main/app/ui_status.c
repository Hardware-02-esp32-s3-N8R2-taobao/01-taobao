#include "ui_status.h"

#include <stdio.h>
#include "oled_ssd1306.h"

void ui_status_render(bool led_on, uint32_t blink_count)
{
    char line1[24];
    char line2[24];
    int filled_segments = (int)(blink_count % 10U);

    snprintf(line1, sizeof(line1), "LED:%s", led_on ? "ON" : "OFF");
    snprintf(line2, sizeof(line2), "COUNT:%lu", (unsigned long)blink_count);

    oled_ssd1306_clear();
    oled_ssd1306_draw_text(20, 0, "ESP32-C3");
    oled_ssd1306_draw_text(0, 18, line1);
    oled_ssd1306_draw_text(0, 34, "OLED:ACTIVE");
    oled_ssd1306_draw_text(0, 50, line2);

    oled_ssd1306_draw_rect(68, 18, 56, 12, true);
    for (int i = 0; i < filled_segments; i++) {
        oled_ssd1306_fill_rect(71 + i * 5, 21, 3, 6, true);
    }
}
