#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"

#define TAG "oled_demo"

#define I2C_HOST                0
#define OLED_I2C_SDA_GPIO       5
#define OLED_I2C_SCL_GPIO       6
#define OLED_I2C_ADDR_PRIMARY   0x3C
#define OLED_I2C_ADDR_SECONDARY 0x3D
#define OLED_PIXEL_CLOCK_HZ     (400 * 1000)
#define OLED_H_RES              128
#define OLED_V_RES              64
#define OLED_CMD_BITS           8
#define OLED_PARAM_BITS         8
#define OLED_RESET_GPIO         -1
#define UI_REFRESH_MS           3000
static uint8_t s_oled_buffer[OLED_H_RES * OLED_V_RES / 8];
static int s_update_count;
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
            TAG,
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
    case ':': return (const uint8_t[5]){0x00, 0x36, 0x36, 0x00, 0x00};
    case '.': return (const uint8_t[5]){0x00, 0x60, 0x60, 0x00, 0x00};
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
    case 'C': return (const uint8_t[5]){0x3E, 0x41, 0x41, 0x41, 0x22};
    case 'D': return (const uint8_t[5]){0x7F, 0x41, 0x41, 0x22, 0x1C};
    case 'E': return (const uint8_t[5]){0x7F, 0x49, 0x49, 0x49, 0x41};
    case 'F': return (const uint8_t[5]){0x7F, 0x09, 0x09, 0x09, 0x01};
    case 'H': return (const uint8_t[5]){0x7F, 0x08, 0x08, 0x08, 0x7F};
    case 'I': return (const uint8_t[5]){0x00, 0x41, 0x7F, 0x41, 0x00};
    case 'M': return (const uint8_t[5]){0x7F, 0x02, 0x0C, 0x02, 0x7F};
    case 'P': return (const uint8_t[5]){0x7F, 0x09, 0x09, 0x09, 0x06};
    case 'R': return (const uint8_t[5]){0x7F, 0x09, 0x19, 0x29, 0x46};
    case 'S': return (const uint8_t[5]){0x46, 0x49, 0x49, 0x49, 0x31};
    case 'T': return (const uint8_t[5]){0x01, 0x01, 0x7F, 0x01, 0x01};
    case 'U': return (const uint8_t[5]){0x3F, 0x40, 0x40, 0x40, 0x3F};
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

    uint16_t index = x + (y / 8) * OLED_H_RES;
    uint8_t mask = 1 << (y % 8);

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
            bool on = glyph[col] & (1 << row);
            oled_set_pixel(x + col, y + row, on);
        }
    }
}

static void oled_draw_text(int x, int y, const char *text)
{
    while (*text) {
        oled_draw_char(x, y, *text++);
        x += 6;
    }
}

static esp_err_t oled_flush(void)
{
    for (int page = 0; page < OLED_V_RES / 8; page++) {
        ESP_RETURN_ON_ERROR(oled_write_command((uint8_t)(0xB0 + page)), TAG, "set page failed");
        ESP_RETURN_ON_ERROR(oled_write_command(0x00), TAG, "set low column failed");
        ESP_RETURN_ON_ERROR(oled_write_command(0x10), TAG, "set high column failed");
        ESP_RETURN_ON_ERROR(oled_write_data(&s_oled_buffer[OLED_H_RES * page], OLED_H_RES), TAG, "page write failed");
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
        ESP_RETURN_ON_ERROR(oled_write_command(init_cmds[i]), TAG, "init command failed");
    }

    return ESP_OK;
}

static esp_err_t oled_probe(uint8_t addr)
{
    uint8_t buffer[2] = {0x00, 0xAE};
    return i2c_master_write_to_device(I2C_HOST, addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static void oled_render_frame(void)
{
    char line1[24];
    char line2[24];
    char line3[24];

    float temp = 24.5f + (float)(s_update_count % 7) * 0.6f;
    int humi = 48 + (s_update_count * 3) % 18;

    snprintf(line1, sizeof(line1), "TEMP:%2.1fC", temp);
    snprintf(line2, sizeof(line2), "HUMI:%2d%%", humi);
    snprintf(line3, sizeof(line3), "REFRESH:%02d", s_update_count + 1);

    oled_clear_buffer();
    oled_draw_text(18, 0, "YD-ESP32-S3");
    oled_draw_text(4, 20, line1);
    oled_draw_text(4, 36, line2);
    oled_draw_text(4, 52, line3);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Start OLED demo");
    ESP_LOGI(TAG, "Connect OLED VCC->3V3 GND->GND SDA->GPIO%d SCL->GPIO%d",
             OLED_I2C_SDA_GPIO, OLED_I2C_SCL_GPIO);

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_I2C_SDA_GPIO,
        .scl_io_num = OLED_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_PIXEL_CLOCK_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST, I2C_MODE_MASTER, 0, 0, 0));

    if (oled_probe(OLED_I2C_ADDR_PRIMARY) == ESP_OK) {
        s_oled_addr = OLED_I2C_ADDR_PRIMARY;
    } else if (oled_probe(OLED_I2C_ADDR_SECONDARY) == ESP_OK) {
        s_oled_addr = OLED_I2C_ADDR_SECONDARY;
    } else {
        ESP_LOGE(TAG, "No OLED found on I2C addr 0x3C or 0x3D. Check wiring.");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    ESP_LOGI(TAG, "OLED detected at addr=0x%02X", s_oled_addr);
    ESP_ERROR_CHECK(oled_panel_init());

    while (1) {
        oled_render_frame();
        ESP_ERROR_CHECK(oled_flush());

        float temp = 24.5f + (float)(s_update_count % 7) * 0.6f;
        int humi = 48 + (s_update_count * 3) % 18;
        ESP_LOGI(TAG, "Refresh OLED: temp=%.1fC humi=%d%%", temp, humi);

        s_update_count++;
        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}
