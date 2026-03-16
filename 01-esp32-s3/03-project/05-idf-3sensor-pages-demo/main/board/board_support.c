#include "board/board_support.h"

#include <stdbool.h>

#include "esp_log.h"
#include "led_strip.h"

#include "app/app_config.h"

static const char *TAG = APP_TAG;
static led_strip_handle_t s_rgb_led;
static bool s_rgb_ready;

static esp_err_t ensure_rgb_ready(void)
{
    if (s_rgb_ready) {
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = RGB_LED_COUNT,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = RGB_RMT_RESOLUTION_HZ,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_rgb_led);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board RGB LED init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_rgb_ready = true;
    ESP_LOGI(TAG, "Board RGB LED ready on GPIO%d", RGB_LED_GPIO);
    return ESP_OK;
}

esp_err_t board_rgb_init(void)
{
    return ensure_rgb_ready();
}

esp_err_t board_rgb_set(uint8_t red, uint8_t green, uint8_t blue)
{
    esp_err_t ret = ensure_rgb_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = led_strip_set_pixel(s_rgb_led, 0, red, green, blue);
    if (ret == ESP_OK) {
        ret = led_strip_refresh(s_rgb_led);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board RGB set failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t board_rgb_off(void)
{
    esp_err_t ret = ensure_rgb_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = led_strip_clear(s_rgb_led);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board RGB off failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}
