#include "oled_display/oled_display.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"

#include "app/app_config.h"
#include "i2c_bus/i2c_bus.h"

static uint8_t s_oled_buffer[OLED_H_RES * OLED_V_RES / 8];
static bool s_oled_ready;
static uint8_t s_oled_addr = OLED_I2C_ADDR_PRIMARY;

static esp_err_t oled_write_command(uint8_t command)
{
    uint8_t buffer[2] = {0x00, command};
    return i2c_master_write_to_device(I2C_HOST, s_oled_addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static esp_err_t oled_write_data(const uint8_t *data, size_t data_len)
{
    uint8_t tx[17];
    tx[0] = 0x40;

    while (data_len > 0) {
        size_t chunk = data_len > 16 ? 16 : data_len;
        memcpy(&tx[1], data, chunk);
        ESP_RETURN_ON_ERROR(
            i2c_master_write_to_device(I2C_HOST, s_oled_addr, tx, chunk + 1, pdMS_TO_TICKS(100)),
            APP_TAG,
            "oled data write failed"
        );
        data += chunk;
        data_len -= chunk;
    }

    return ESP_OK;
}

static const uint8_t *font5x7(char c)
{
    static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

    switch (c) {
    case ' ': return (const uint8_t[5]){0x00, 0x00, 0x00, 0x00, 0x00};
    case '-': return (const uint8_t[5]){0x08, 0x08, 0x08, 0x08, 0x08};
    case '.': return (const uint8_t[5]){0x00, 0x60, 0x60, 0x00, 0x00};
    case '/': return (const uint8_t[5]){0x20, 0x10, 0x08, 0x04, 0x02};
    case ':': return (const uint8_t[5]){0x00, 0x36, 0x36, 0x00, 0x00};
    case '%': return (const uint8_t[5]){0x62, 0x64, 0x08, 0x13, 0x23};
    case '0': return (const uint8_t[5]){0x3E, 0x51, 0x49, 0x45, 0x3E};
    case '1': return (const uint8_t[5]){0x00, 0x42, 0x7F, 0x40, 0x00};
    case '2': return (const uint8_t[5]){0x42, 0x61, 0x51, 0x49, 0x46};
    case '3': return (const uint8_t[5]){0x21, 0x41, 0x45, 0x4B, 0x31};
    case '4': return (const uint8_t[5]){0x18, 0x14, 0x12, 0x7F, 0x10};
    case '5': return (const uint8_t[5]){0x27, 0x45, 0x45, 0x45, 0x39};
    case '6': return (const uint8_t[5]){0x3C, 0x4A, 0x49, 0x49, 0x30};
    case '7': return (const uint8_t[5]){0x01, 0x71, 0x09, 0x05, 0x03};
    case '8': return (const uint8_t[5]){0x36, 0x49, 0x49, 0x49, 0x36};
    case '9': return (const uint8_t[5]){0x06, 0x49, 0x49, 0x29, 0x1E};
    case 'A': return (const uint8_t[5]){0x7E, 0x11, 0x11, 0x11, 0x7E};
    case 'B': return (const uint8_t[5]){0x7F, 0x49, 0x49, 0x49, 0x36};
    case 'C': return (const uint8_t[5]){0x3E, 0x41, 0x41, 0x41, 0x22};
    case 'D': return (const uint8_t[5]){0x7F, 0x41, 0x41, 0x22, 0x1C};
    case 'E': return (const uint8_t[5]){0x7F, 0x49, 0x49, 0x49, 0x41};
    case 'G': return (const uint8_t[5]){0x3E, 0x41, 0x49, 0x49, 0x7A};
    case 'H': return (const uint8_t[5]){0x7F, 0x08, 0x08, 0x08, 0x7F};
    case 'I': return (const uint8_t[5]){0x00, 0x41, 0x7F, 0x41, 0x00};
    case 'K': return (const uint8_t[5]){0x7F, 0x08, 0x14, 0x22, 0x41};
    case 'L': return (const uint8_t[5]){0x7F, 0x40, 0x40, 0x40, 0x40};
    case 'M': return (const uint8_t[5]){0x7F, 0x02, 0x0C, 0x02, 0x7F};
    case 'N': return (const uint8_t[5]){0x7F, 0x06, 0x08, 0x30, 0x7F};
    case 'O': return (const uint8_t[5]){0x3E, 0x41, 0x41, 0x41, 0x3E};
    case 'P': return (const uint8_t[5]){0x7F, 0x09, 0x09, 0x09, 0x06};
    case 'Q': return (const uint8_t[5]){0x3E, 0x41, 0x51, 0x21, 0x5E};
    case 'R': return (const uint8_t[5]){0x7F, 0x09, 0x19, 0x29, 0x46};
    case 'S': return (const uint8_t[5]){0x46, 0x49, 0x49, 0x49, 0x31};
    case 'T': return (const uint8_t[5]){0x01, 0x01, 0x7F, 0x01, 0x01};
    case 'U': return (const uint8_t[5]){0x3F, 0x40, 0x40, 0x40, 0x3F};
    case 'V': return (const uint8_t[5]){0x1F, 0x20, 0x40, 0x20, 0x1F};
    case 'W': return (const uint8_t[5]){0x7F, 0x20, 0x18, 0x20, 0x7F};
    case 'X': return (const uint8_t[5]){0x63, 0x14, 0x08, 0x14, 0x63};
    case 'Y': return (const uint8_t[5]){0x03, 0x04, 0x78, 0x04, 0x03};
    default: return blank;
    }
}

static void oled_clear_buffer(void)
{
    memset(s_oled_buffer, 0x00, sizeof(s_oled_buffer));
}

static void oled_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_H_RES || y < 0 || y >= OLED_V_RES) {
        return;
    }

    uint16_t index = (uint16_t)x + (uint16_t)(y / 8) * OLED_H_RES;
    uint8_t mask = (uint8_t)(1U << (y % 8));

    if (on) {
        s_oled_buffer[index] |= mask;
    } else {
        s_oled_buffer[index] &= (uint8_t)~mask;
    }
}

static void oled_draw_char(int x, int y, char c)
{
    const uint8_t *glyph = font5x7(c);

    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            bool on = (glyph[col] & (1 << row)) != 0;
            oled_set_pixel(x + col, y + row, on);
        }
    }
}

static void oled_draw_text(int x, int y, const char *text)
{
    while (*text != '\0') {
        oled_draw_char(x, y, *text++);
        x += 6;
    }
}

static int oled_text_width(const char *text)
{
    size_t len = strlen(text);
    if (len == 0) {
        return 0;
    }
    return (int)(len * 6U) - 1;
}

static void oled_draw_text_center(int y, const char *text)
{
    int x = (OLED_H_RES - oled_text_width(text)) / 2;
    if (x < 0) {
        x = 0;
    }
    oled_draw_text(x, y, text);
}

static void oled_draw_text_right(int y, const char *text)
{
    int x = OLED_H_RES - oled_text_width(text);
    if (x < 0) {
        x = 0;
    }
    oled_draw_text(x, y, text);
}

static esp_err_t oled_flush(void)
{
    for (int page = 0; page < OLED_V_RES / 8; page++) {
        ESP_RETURN_ON_ERROR(oled_write_command((uint8_t)(0xB0 + page)), APP_TAG, "set page failed");
        ESP_RETURN_ON_ERROR(oled_write_command(0x00), APP_TAG, "set low column failed");
        ESP_RETURN_ON_ERROR(oled_write_command(0x10), APP_TAG, "set high column failed");
        ESP_RETURN_ON_ERROR(oled_write_data(&s_oled_buffer[OLED_H_RES * page], OLED_H_RES), APP_TAG, "page write failed");
    }
    return ESP_OK;
}

static esp_err_t oled_panel_init(void)
{
    const uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ESP_RETURN_ON_ERROR(oled_write_command(init_cmds[i]), APP_TAG, "init command failed");
    }
    return ESP_OK;
}

esp_err_t oled_display_init(void)
{
    s_oled_ready = false;
    s_oled_addr = OLED_I2C_ADDR_PRIMARY;

    if (i2c_bus_probe(OLED_I2C_ADDR_PRIMARY) == ESP_OK) {
        s_oled_addr = OLED_I2C_ADDR_PRIMARY;
        s_oled_ready = true;
    } else if (i2c_bus_probe(OLED_I2C_ADDR_SECONDARY) == ESP_OK) {
        s_oled_addr = OLED_I2C_ADDR_SECONDARY;
        s_oled_ready = true;
    }

    if (!s_oled_ready) {
        return ESP_ERR_NOT_FOUND;
    }

    return oled_panel_init();
}

bool oled_display_is_ready(void)
{
    return s_oled_ready;
}

void oled_display_render_text4(const char *line1, const char *line2, const char *line3, const char *line4)
{
    if (!s_oled_ready) {
        return;
    }

    oled_clear_buffer();
    oled_draw_text(0, 0, line1);
    oled_draw_text(0, 16, line2);
    oled_draw_text(0, 32, line3);
    oled_draw_text(0, 48, line4);
    ESP_ERROR_CHECK(oled_flush());
}

void oled_display_render_sensor_page(
    const char *page_text,
    const char *wifi_text,
    const char *server_text,
    const char *line2,
    const char *line3,
    const char *line4
)
{
    if (!s_oled_ready) {
        return;
    }

    oled_clear_buffer();
    oled_draw_text(0, 0, page_text);
    oled_draw_text_center(0, wifi_text);
    oled_draw_text_right(0, server_text);
    oled_draw_text(0, 16, line2);
    oled_draw_text(0, 32, line3);
    oled_draw_text(0, 48, line4);
    ESP_ERROR_CHECK(oled_flush());
}
