#include "oled_ssd1306.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"

#define OLED_TAG "oled_ssd1306"
#define OLED_H_RES 128
#define OLED_V_RES 64

static oled_ssd1306_config_t s_oled_cfg;
static uint8_t s_oled_addr;
static uint8_t s_oled_buffer[OLED_H_RES * OLED_V_RES / 8];
static bool s_oled_ready;

static esp_err_t oled_write_command(uint8_t command)
{
    uint8_t buffer[2] = {0x00, command};
    return i2c_master_write_to_device(s_oled_cfg.i2c_port, s_oled_addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static esp_err_t oled_write_data(const uint8_t *data, size_t data_len)
{
    uint8_t tx[17];
    tx[0] = 0x40;

    while (data_len > 0) {
        size_t chunk = data_len > 16 ? 16 : data_len;
        memcpy(&tx[1], data, chunk);
        ESP_RETURN_ON_ERROR(
            i2c_master_write_to_device(s_oled_cfg.i2c_port, s_oled_addr, tx, chunk + 1, pdMS_TO_TICKS(100)),
            OLED_TAG,
            "oled data write failed"
        );
        data += chunk;
        data_len -= chunk;
    }

    return ESP_OK;
}

static esp_err_t oled_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(s_oled_cfg.i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static const uint8_t *font5x7(char c)
{
    static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

    switch (c) {
    case ' ': return (const uint8_t[5]){0x00, 0x00, 0x00, 0x00, 0x00};
    case '-': return (const uint8_t[5]){0x08, 0x08, 0x08, 0x08, 0x08};
    case ':': return (const uint8_t[5]){0x00, 0x36, 0x36, 0x00, 0x00};
    case '.': return (const uint8_t[5]){0x00, 0x60, 0x60, 0x00, 0x00};
    case '/': return (const uint8_t[5]){0x20, 0x10, 0x08, 0x04, 0x02};
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
    case 'C': return (const uint8_t[5]){0x3E, 0x41, 0x41, 0x41, 0x22};
    case 'D': return (const uint8_t[5]){0x7F, 0x41, 0x41, 0x22, 0x1C};
    case 'E': return (const uint8_t[5]){0x7F, 0x49, 0x49, 0x49, 0x41};
    case 'F': return (const uint8_t[5]){0x7F, 0x09, 0x09, 0x09, 0x01};
    case 'H': return (const uint8_t[5]){0x7F, 0x08, 0x08, 0x08, 0x7F};
    case 'I': return (const uint8_t[5]){0x00, 0x41, 0x7F, 0x41, 0x00};
    case 'K': return (const uint8_t[5]){0x7F, 0x08, 0x14, 0x22, 0x41};
    case 'L': return (const uint8_t[5]){0x7F, 0x40, 0x40, 0x40, 0x40};
    case 'M': return (const uint8_t[5]){0x7F, 0x02, 0x0C, 0x02, 0x7F};
    case 'N': return (const uint8_t[5]){0x7F, 0x02, 0x0C, 0x10, 0x7F};
    case 'O': return (const uint8_t[5]){0x3E, 0x41, 0x41, 0x41, 0x3E};
    case 'P': return (const uint8_t[5]){0x7F, 0x09, 0x09, 0x09, 0x06};
    case 'Q': return (const uint8_t[5]){0x3E, 0x41, 0x51, 0x21, 0x5E};
    case 'R': return (const uint8_t[5]){0x7F, 0x09, 0x19, 0x29, 0x46};
    case 'S': return (const uint8_t[5]){0x46, 0x49, 0x49, 0x49, 0x31};
    case 'T': return (const uint8_t[5]){0x01, 0x01, 0x7F, 0x01, 0x01};
    case 'W': return (const uint8_t[5]){0x7F, 0x20, 0x18, 0x20, 0x7F};
    default: return blank;
    }
}

static void oled_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_H_RES || y < 0 || y >= OLED_V_RES) {
        return;
    }

    uint16_t index = (uint16_t)x + (uint16_t)(y / 8) * OLED_H_RES;
    uint8_t mask = 1U << (y % 8);

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
            oled_set_pixel(x + col, y + row, (glyph[col] & (1 << row)) != 0);
        }
    }
}

static esp_err_t oled_panel_init(void)
{
    const uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ESP_RETURN_ON_ERROR(oled_write_command(init_cmds[i]), OLED_TAG, "init command failed");
    }

    return ESP_OK;
}

esp_err_t oled_ssd1306_init(const oled_ssd1306_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, OLED_TAG, "config is null");

    s_oled_cfg = *config;
    s_oled_ready = false;
    s_oled_addr = s_oled_cfg.primary_addr;

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = s_oled_cfg.sda_gpio,
        .scl_io_num = s_oled_cfg.scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = s_oled_cfg.pixel_clock_hz,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(s_oled_cfg.i2c_port, &i2c_conf), OLED_TAG, "i2c config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(s_oled_cfg.i2c_port, I2C_MODE_MASTER, 0, 0, 0), OLED_TAG, "i2c install failed");

    if (oled_probe(s_oled_cfg.primary_addr) == ESP_OK) {
        s_oled_addr = s_oled_cfg.primary_addr;
        s_oled_ready = true;
    } else if (oled_probe(s_oled_cfg.secondary_addr) == ESP_OK) {
        s_oled_addr = s_oled_cfg.secondary_addr;
        s_oled_ready = true;
    } else {
        return ESP_ERR_NOT_FOUND;
    }

    oled_ssd1306_clear();
    return oled_panel_init();
}

bool oled_ssd1306_is_ready(void)
{
    return s_oled_ready;
}

uint8_t oled_ssd1306_get_address(void)
{
    return s_oled_addr;
}

void oled_ssd1306_clear(void)
{
    memset(s_oled_buffer, 0x00, sizeof(s_oled_buffer));
}

void oled_ssd1306_draw_text(int x, int y, const char *text)
{
    while (*text) {
        oled_draw_char(x, y, *text++);
        x += 6;
    }
}

void oled_ssd1306_draw_rect(int x, int y, int w, int h, bool on)
{
    for (int dx = 0; dx < w; dx++) {
        oled_set_pixel(x + dx, y, on);
        oled_set_pixel(x + dx, y + h - 1, on);
    }

    for (int dy = 0; dy < h; dy++) {
        oled_set_pixel(x, y + dy, on);
        oled_set_pixel(x + w - 1, y + dy, on);
    }
}

void oled_ssd1306_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            oled_set_pixel(x + dx, y + dy, on);
        }
    }
}

esp_err_t oled_ssd1306_present(void)
{
    ESP_RETURN_ON_FALSE(s_oled_ready, ESP_ERR_INVALID_STATE, OLED_TAG, "oled not ready");

    for (int page = 0; page < OLED_V_RES / 8; page++) {
        ESP_RETURN_ON_ERROR(oled_write_command((uint8_t)(0xB0 + page)), OLED_TAG, "set page failed");
        ESP_RETURN_ON_ERROR(oled_write_command(0x00), OLED_TAG, "set low column failed");
        ESP_RETURN_ON_ERROR(oled_write_command(0x10), OLED_TAG, "set high column failed");
        ESP_RETURN_ON_ERROR(oled_write_data(&s_oled_buffer[OLED_H_RES * page], OLED_H_RES), OLED_TAG, "page write failed");
    }

    return ESP_OK;
}
