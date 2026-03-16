#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ds18b20_demo"

#define I2C_HOST                I2C_NUM_0
#define I2C_SDA_GPIO            5
#define I2C_SCL_GPIO            6
#define I2C_CLOCK_HZ            (400 * 1000)
#define UI_REFRESH_MS           3000

#define OLED_I2C_ADDR_PRIMARY   0x3C
#define OLED_I2C_ADDR_SECONDARY 0x3D
#define OLED_H_RES              128
#define OLED_V_RES              64

#define DS18B20_GPIO            4
#define DS18B20_CMD_SKIP_ROM    0xCC
#define DS18B20_CMD_CONVERT_T   0x44
#define DS18B20_CMD_READ_PAD    0xBE

static uint8_t s_oled_buffer[OLED_H_RES * OLED_V_RES / 8];
static bool s_oled_ready;
static uint8_t s_oled_addr = OLED_I2C_ADDR_PRIMARY;
static int s_report_count;

static esp_err_t i2c_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_HOST, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

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
    case '.': return (const uint8_t[5]){0x00, 0x60, 0x60, 0x00, 0x00};
    case ':': return (const uint8_t[5]){0x00, 0x36, 0x36, 0x00, 0x00};
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

static void oled_render_text4(const char *line1, const char *line2, const char *line3, const char *line4)
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

static void ds18b20_bus_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << DS18B20_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(DS18B20_GPIO, 1);
}

static inline void ds18b20_bus_low(void)
{
    gpio_set_level(DS18B20_GPIO, 0);
}

static inline void ds18b20_bus_release(void)
{
    gpio_set_level(DS18B20_GPIO, 1);
}

static bool ds18b20_reset_pulse(void)
{
    ds18b20_bus_low();
    esp_rom_delay_us(480);
    ds18b20_bus_release();
    esp_rom_delay_us(70);
    bool present = gpio_get_level(DS18B20_GPIO) == 0;
    esp_rom_delay_us(410);
    return present;
}

static void ds18b20_write_bit(int bit)
{
    ds18b20_bus_low();
    if (bit) {
        esp_rom_delay_us(6);
        ds18b20_bus_release();
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        ds18b20_bus_release();
        esp_rom_delay_us(10);
    }
}

static int ds18b20_read_bit(void)
{
    ds18b20_bus_low();
    esp_rom_delay_us(6);
    ds18b20_bus_release();
    esp_rom_delay_us(9);
    int bit = gpio_get_level(DS18B20_GPIO);
    esp_rom_delay_us(55);
    return bit;
}

static void ds18b20_write_byte(uint8_t value)
{
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(value & 0x01);
        value >>= 1;
    }
}

static uint8_t ds18b20_read_byte(void)
{
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        if (ds18b20_read_bit()) {
            value |= (uint8_t)(1U << i);
        }
    }
    return value;
}

static uint8_t ds18b20_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t inbyte = data[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }

    return crc;
}

static esp_err_t ds18b20_read_temperature(float *temp_c)
{
    uint8_t scratchpad[9];

    if (!ds18b20_reset_pulse()) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(DS18B20_CMD_CONVERT_T);
    vTaskDelay(pdMS_TO_TICKS(800));

    if (!ds18b20_reset_pulse()) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(DS18B20_CMD_READ_PAD);

    for (int i = 0; i < 9; i++) {
        scratchpad[i] = ds18b20_read_byte();
    }

    if (ds18b20_crc8(scratchpad, 8) != scratchpad[8]) {
        return ESP_ERR_INVALID_CRC;
    }

    int16_t raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
    *temp_c = raw / 16.0f;
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Start DS18B20 demo");
    ESP_LOGI(TAG, "OLED I2C: SDA=GPIO%d SCL=GPIO%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    ESP_LOGI(TAG, "DS18B20 data pin: GPIO%d", DS18B20_GPIO);

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_CLOCK_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST, I2C_MODE_MASTER, 0, 0, 0));

    if (i2c_probe(OLED_I2C_ADDR_PRIMARY) == ESP_OK) {
        s_oled_addr = OLED_I2C_ADDR_PRIMARY;
        s_oled_ready = true;
    } else if (i2c_probe(OLED_I2C_ADDR_SECONDARY) == ESP_OK) {
        s_oled_addr = OLED_I2C_ADDR_SECONDARY;
        s_oled_ready = true;
    }

    if (s_oled_ready) {
        ESP_LOGI(TAG, "OLED detected at 0x%02X", s_oled_addr);
        ESP_ERROR_CHECK(oled_panel_init());
        oled_render_text4("DS18B20", "GPIO4", "WAIT TEMP", "");
    } else {
        ESP_LOGW(TAG, "OLED not found at 0x3C/0x3D");
    }

    ds18b20_bus_init();

    while (1) {
        float temp_c = 0.0f;
        esp_err_t ret = ds18b20_read_temperature(&temp_c);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "DS18B20 read failed: %s", esp_err_to_name(ret));
            oled_render_text4("NO SENSOR", "CHECK GPIO4", "PULLUP 4K7", "VDD 3V3");
            vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
            continue;
        }

        char line2[24];
        char line3[24];
        char line4[24];

        snprintf(line2, sizeof(line2), "TEMP:%2.2fC", temp_c);
        snprintf(line3, sizeof(line3), "GPIO4");
        snprintf(line4, sizeof(line4), "CNT:%02d", s_report_count + 1);
        oled_render_text4("DS18B20", line2, line3, line4);

        s_report_count++;
        ESP_LOGI(TAG, "{\"seq\":%d,\"sensor\":\"DS18B20\",\"temp_c\":%.2f}", s_report_count, temp_c);
        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}
