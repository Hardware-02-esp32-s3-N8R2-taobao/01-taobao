#include "status_led.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "device_profile.h"

static bool s_initialized = false;
static bool s_enabled = false;

static int inactive_level(void)
{
    return APP_STATUS_LED_ACTIVE_LEVEL ? 0 : 1;
}

esp_err_t status_led_init(void)
{
    const device_hw_variant_t hw_variant = device_profile_hardware_variant();
    if (hw_variant != DEVICE_HW_VARIANT_SUPERMINI && hw_variant != DEVICE_HW_VARIANT_UNKNOWN) {
        s_enabled = false;
        s_initialized = true;
        return ESP_OK;
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << APP_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_set_level(APP_STATUS_LED_GPIO, inactive_level());
    if (ret != ESP_OK) {
        return ret;
    }

    s_enabled = true;
    s_initialized = true;
    return ESP_OK;
}

void status_led_blink_publish(void)
{
    if (!s_initialized || !s_enabled) {
        return;
    }

    if (gpio_set_level(APP_STATUS_LED_GPIO, APP_STATUS_LED_ACTIVE_LEVEL) != ESP_OK) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(APP_STATUS_LED_BLINK_MS));
    (void)gpio_set_level(APP_STATUS_LED_GPIO, inactive_level());
}
