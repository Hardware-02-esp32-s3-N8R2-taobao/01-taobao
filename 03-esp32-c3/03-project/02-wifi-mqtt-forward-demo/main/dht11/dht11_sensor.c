#include "dht11_sensor.h"

#include <string.h>
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static gpio_num_t s_dht11_gpio = GPIO_NUM_NC;
static dht11_sample_t s_last_dht11_sample;

static esp_err_t dht11_wait_level(int level, uint32_t timeout_us)
{
    while (timeout_us--) {
        if (gpio_get_level(s_dht11_gpio) == level) {
            return ESP_OK;
        }
        esp_rom_delay_us(1);
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t dht11_read_sample_raw(dht11_sample_t *sample)
{
    uint8_t data[5] = {0};
    memset(sample, 0, sizeof(*sample));

    // Drive the bus low for the start signal, then release it and switch to input
    // so the DHT11 can pull the line for its response waveform.
    gpio_set_direction(s_dht11_gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(s_dht11_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(s_dht11_gpio, 1);
    esp_rom_delay_us(40);
    gpio_set_direction(s_dht11_gpio, GPIO_MODE_INPUT);
    gpio_pullup_en(s_dht11_gpio);

    if (dht11_wait_level(0, 100) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (dht11_wait_level(1, 100) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (dht11_wait_level(0, 100) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < 40; i++) {
        if (dht11_wait_level(1, 100) != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        esp_rom_delay_us(35);
        int bit = gpio_get_level(s_dht11_gpio);
        if (dht11_wait_level(0, 100) != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        data[i / 8] = (uint8_t)((data[i / 8] << 1) | (bit ? 1 : 0));
    }

    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    sample->ready = true;
    sample->humidity_pct = (float)data[0];
    sample->temperature_c = (float)data[2];
    return ESP_OK;
}

void dht11_sensor_init(gpio_num_t gpio)
{
    s_dht11_gpio = gpio;
    s_last_dht11_sample = (dht11_sample_t){0};

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(gpio, 1);
}

esp_err_t dht11_sensor_read(dht11_sample_t *sample)
{
    esp_err_t last_err = ESP_FAIL;

    if (s_dht11_gpio == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        last_err = dht11_read_sample_raw(sample);
        if (last_err == ESP_OK) {
            s_last_dht11_sample = *sample;
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }

    if (s_last_dht11_sample.ready) {
        *sample = s_last_dht11_sample;
        return ESP_OK;
    }

    return last_err;
}
