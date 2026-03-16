#include "ui/ui_pages.h"

#include <stdio.h>

#include "app/app_config.h"
#include "oled_display/oled_display.h"
#include "pressure/pressure_sensor.h"

int ui_pages_count(void)
{
    return UI_PAGE_COUNT;
}

void ui_render_boot_screen(void)
{
    oled_display_render_text4("7 PAGE DEMO", "INIT", "WAIT DATA", "");
}

static void render_pressure_page(const pressure_sample_t *sample, const char *wifi_text, const char *server_text)
{
    char line2[24];
    char line3[24];
    char line4[24];

    if (!sample->ready) {
        oled_display_render_sensor_page("1/7", wifi_text, server_text, "PRESSURE", "P: ----.- PA", "T: --.-- C");
        return;
    }

    snprintf(line2, sizeof(line2), "%s", pressure_sensor_label(pressure_sensor_type()));
    snprintf(line3, sizeof(line3), "P: %.1f PA", sample->pressure_pa);
    snprintf(line4, sizeof(line4), "T: %.2f A: %.1f", sample->temperature_c, sample->altitude_m);
    oled_display_render_sensor_page("1/7", wifi_text, server_text, line2, line3, line4);
}

static void render_ds18_page(const ds18b20_sample_t *sample, const char *wifi_text, const char *server_text)
{
    char line3[24];

    if (!sample->ready) {
        oled_display_render_sensor_page("2/7", wifi_text, server_text, "DS18B20", "TEMP: --.-- C", "");
        return;
    }

    snprintf(line3, sizeof(line3), "TEMP: %.2f C", sample->temperature_c);
    oled_display_render_sensor_page("2/7", wifi_text, server_text, "DS18B20", line3, "");
}

static void render_dht11_page(const dht11_sample_t *sample, const char *wifi_text, const char *server_text)
{
    char line3[24];
    char line4[24];

    if (!sample->ready) {
        oled_display_render_sensor_page("3/7", wifi_text, server_text, "DHT11", "TEMP: --.- C", "HUM: --.- %");
        return;
    }

    snprintf(line3, sizeof(line3), "TEMP: %.1f C", sample->temperature_c);
    snprintf(line4, sizeof(line4), "HUM: %.1f %%", sample->humidity_pct);
    oled_display_render_sensor_page("3/7", wifi_text, server_text, "DHT11", line3, line4);
}

static void render_bh1750_page(const bh1750_sample_t *sample, const char *wifi_text, const char *server_text)
{
    char line3[24];

    if (!sample->ready) {
        oled_display_render_sensor_page("4/7", wifi_text, server_text, "BH1750", "LIGHT: --.- LX", "");
        return;
    }

    snprintf(line3, sizeof(line3), "LIGHT: %.1f LX", sample->lux);
    oled_display_render_sensor_page("4/7", wifi_text, server_text, "BH1750", line3, "");
}

static void render_soil_page(const soil_moisture_sample_t *sample, const char *wifi_text, const char *server_text)
{
    char line3[24];
    char line4[24];

    if (!sample->ready) {
        oled_display_render_sensor_page("5/7", wifi_text, server_text, "SOIL", "MOIST: --.- %", "RAW: ----");
        return;
    }

    snprintf(line3, sizeof(line3), "MOIST: %.1f %%", sample->moisture_pct);
    snprintf(line4, sizeof(line4), "RAW: %d", sample->raw);
    oled_display_render_sensor_page("5/7", wifi_text, server_text, "SOIL", line3, line4);
}

static void render_rain_page(const rain_sensor_sample_t *sample, const char *wifi_text, const char *server_text)
{
    char line3[24];
    char line4[24];

    if (!sample->ready) {
        oled_display_render_sensor_page("6/7", wifi_text, server_text, "RAIN", "STATE: WAIT", "LVL: --.- %");
        return;
    }

    snprintf(line3, sizeof(line3), "STATE:%s", sample->is_raining ? "RAIN" : "CLEAR");
    snprintf(line4, sizeof(line4), "LVL: %.1f %%", sample->rain_level_pct);
    oled_display_render_sensor_page("6/7", wifi_text, server_text, "RAIN", line3, line4);
}

static void render_pump_page(const pump_state_t *pump, const char *wifi_text, const char *server_text)
{
    char line3[24];
    char line4[24];

    if (!pump->command_received) {
        oled_display_render_sensor_page("7/7", wifi_text, server_text, "PUMP", "STATE: IDLE", "WAIT MQTT");
        return;
    }

    snprintf(
        line3,
        sizeof(line3),
        "%s %lus",
        pump->active ? "STATE:ON" : "STATE:OFF",
        (unsigned long)pump->remaining_seconds
    );
    snprintf(line4, sizeof(line4), "BY: %.15s", pump->requested_by[0] != '\0' ? pump->requested_by : "mqtt");
    oled_display_render_sensor_page("7/7", wifi_text, server_text, "PUMP", line3, line4);
}

void ui_render_current_page(int page_index, const app_samples_t *samples, const char *wifi_text, const char *server_text)
{
    switch (page_index % UI_PAGE_COUNT) {
    case 0:
        render_pressure_page(&samples->pressure, wifi_text, server_text);
        break;
    case 1:
        render_ds18_page(&samples->ds18b20, wifi_text, server_text);
        break;
    case 2:
        render_dht11_page(&samples->dht11, wifi_text, server_text);
        break;
    case 3:
        render_bh1750_page(&samples->bh1750, wifi_text, server_text);
        break;
    case 4:
        render_soil_page(&samples->soil_moisture, wifi_text, server_text);
        break;
    case 5:
        render_rain_page(&samples->rain, wifi_text, server_text);
        break;
    default:
        render_pump_page(&samples->pump, wifi_text, server_text);
        break;
    }
}
