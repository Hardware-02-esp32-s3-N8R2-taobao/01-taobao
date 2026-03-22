#include "ds18b20_sensor.h"

#include "esp_rom_sys.h"

#define DS18B20_CONVERT_T_CMD 0x44
#define DS18B20_READ_SCRATCHPAD_CMD 0xBE
#define DS18B20_SKIP_ROM_CMD 0xCC

static gpio_num_t s_gpio = GPIO_NUM_NC;

static void ow_drive_low(void)
{
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(s_gpio, 0);
}

static void ow_release_bus(void)
{
    gpio_set_level(s_gpio, 1);
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_pullup_en(s_gpio);
}

static int ow_read_level(void)
{
    return gpio_get_level(s_gpio);
}

static esp_err_t ow_reset(void)
{
    ow_drive_low();
    esp_rom_delay_us(480);
    ow_release_bus();
    esp_rom_delay_us(70);
    int presence = ow_read_level();
    esp_rom_delay_us(410);
    return presence == 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void ow_write_bit(int bit)
{
    ow_drive_low();
    if (bit) {
        esp_rom_delay_us(6);
        ow_release_bus();
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        ow_release_bus();
        esp_rom_delay_us(10);
    }
}

static int ow_read_bit(void)
{
    int bit = 0;
    ow_drive_low();
    esp_rom_delay_us(6);
    ow_release_bus();
    esp_rom_delay_us(9);
    bit = ow_read_level();
    esp_rom_delay_us(55);
    return bit;
}

static void ow_write_byte(uint8_t value)
{
    for (int i = 0; i < 8; ++i) {
        ow_write_bit((value >> i) & 0x01);
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= (uint8_t)(ow_read_bit() << i);
    }
    return value;
}

void ds18b20_sensor_init(gpio_num_t gpio)
{
    s_gpio = gpio;
    gpio_reset_pin(s_gpio);
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_pullup_en(s_gpio);
    gpio_set_level(s_gpio, 1);
}

esp_err_t ds18b20_sensor_read(ds18b20_sample_t *sample)
{
    if (sample == NULL || s_gpio == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ow_reset() != ESP_OK) {
        sample->ready = false;
        return ESP_ERR_NOT_FOUND;
    }

    ow_write_byte(DS18B20_SKIP_ROM_CMD);
    ow_write_byte(DS18B20_CONVERT_T_CMD);
    esp_rom_delay_us(750000);

    if (ow_reset() != ESP_OK) {
        sample->ready = false;
        return ESP_ERR_TIMEOUT;
    }

    ow_write_byte(DS18B20_SKIP_ROM_CMD);
    ow_write_byte(DS18B20_READ_SCRATCHPAD_CMD);

    uint8_t data[9] = {0};
    for (int i = 0; i < 9; ++i) {
        data[i] = ow_read_byte();
    }

    int16_t raw_temp = (int16_t)((data[1] << 8) | data[0]);
    sample->temperature_c = (float)raw_temp / 16.0f;
    sample->ready = true;
    return ESP_OK;
}
