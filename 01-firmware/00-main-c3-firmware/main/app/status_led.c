#include "status_led.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "device_profile.h"

static bool s_initialized = false;
static bool s_enabled = false;
static bool s_provisioning_mode = false;
static TaskHandle_t s_led_task_handle = NULL;

static int inactive_level(void)
{
    return APP_STATUS_LED_ACTIVE_LEVEL ? 0 : 1;
}

static void status_led_task(void *arg)
{
    (void)arg;

    while (true) {
        if (!s_initialized || !s_enabled || !s_provisioning_mode) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        (void)gpio_set_level(APP_STATUS_LED_GPIO, APP_STATUS_LED_ACTIVE_LEVEL);
        vTaskDelay(pdMS_TO_TICKS(120));
        (void)gpio_set_level(APP_STATUS_LED_GPIO, inactive_level());
        vTaskDelay(pdMS_TO_TICKS(120));
    }
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
    BaseType_t ok = xTaskCreate(status_led_task, "status_led_task", 2048, NULL, 2, &s_led_task_handle);
    if (ok != pdPASS) {
        s_led_task_handle = NULL;
    }
    return ESP_OK;
}

void status_led_blink_publish(void)
{
    if (!s_initialized || !s_enabled || s_provisioning_mode) {
        return;
    }

    if (gpio_set_level(APP_STATUS_LED_GPIO, APP_STATUS_LED_ACTIVE_LEVEL) != ESP_OK) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(APP_STATUS_LED_BLINK_MS));
    (void)gpio_set_level(APP_STATUS_LED_GPIO, inactive_level());
}

void status_led_set_provisioning(bool enabled)
{
    if (!s_initialized || !s_enabled) {
        return;
    }

    s_provisioning_mode = enabled;
    if (!enabled) {
        (void)gpio_set_level(APP_STATUS_LED_GPIO, inactive_level());
    }
}
