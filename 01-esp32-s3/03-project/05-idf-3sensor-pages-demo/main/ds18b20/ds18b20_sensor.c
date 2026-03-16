#include "ds18b20/ds18b20_sensor.h"

#include <string.h>

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DS18B20_CMD_SKIP_ROM 0xCC
#define DS18B20_CMD_CONVERT_T 0x44
#define DS18B20_CMD_READ_PAD 0xBE

static gpio_num_t s_ds18b20_gpio = GPIO_NUM_NC;

static inline void ds18b20_bus_low(void)
{
    gpio_set_level(s_ds18b20_gpio, 0);
}

static inline void ds18b20_bus_release(void)
{
    gpio_set_level(s_ds18b20_gpio, 1);
}

static bool ds18b20_reset_pulse(void)
{
    ds18b20_bus_low();
    esp_rom_delay_us(480);
    ds18b20_bus_release();
    esp_rom_delay_us(70);
    bool present = gpio_get_level(s_ds18b20_gpio) == 0;
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
    int bit = gpio_get_level(s_ds18b20_gpio);
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

void ds18b20_sensor_init(gpio_num_t gpio)
{
    s_ds18b20_gpio = gpio;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(gpio, 1);
}

esp_err_t ds18b20_sensor_read(ds18b20_sample_t *sample)
{
    uint8_t scratchpad[9];
    memset(sample, 0, sizeof(*sample));

    if (s_ds18b20_gpio == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

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
    sample->ready = true;
    sample->temperature_c = raw / 16.0f;
    return ESP_OK;
}
