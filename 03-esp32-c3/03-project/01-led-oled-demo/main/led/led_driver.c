#include "led_driver.h"

#include <stddef.h>
#include "esp_check.h"

static led_driver_config_t s_led_config;
static bool s_led_initialized;

esp_err_t led_driver_init(const led_driver_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, "led_driver", "config is null");

    s_led_config = *config;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << s_led_config.gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), "led_driver", "gpio config failed");

    s_led_initialized = true;
    return led_driver_set(false);
}

esp_err_t led_driver_set(bool on)
{
    ESP_RETURN_ON_FALSE(s_led_initialized, ESP_ERR_INVALID_STATE, "led_driver", "driver not initialized");

    int level = on ? (int)s_led_config.active_level : (int)!s_led_config.active_level;
    return gpio_set_level(s_led_config.gpio_num, level);
}
