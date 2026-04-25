#include "status_led.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "device_profile.h"

#define STATUS_LED_FAST_BLINK_ON_MS 100
#define STATUS_LED_FAST_BLINK_OFF_MS 100
#define STATUS_LED_SLOW_BLINK_ON_MS 600
#define STATUS_LED_SLOW_BLINK_OFF_MS 600
#define STATUS_LED_STEADY_REFRESH_MS 150

static bool s_initialized = false;
static bool s_enabled = false;
static volatile bool s_provisioning_mode = false;
static volatile status_led_mode_t s_mode = STATUS_LED_MODE_NORMAL;
static TaskHandle_t s_led_task_handle = NULL;

static int inactive_level(void)
{
    return APP_STATUS_LED_ACTIVE_LEVEL ? 0 : 1;
}

static void set_led_active(bool enabled)
{
    if (!s_initialized || !s_enabled) {
        return;
    }

    (void)gpio_set_level(APP_STATUS_LED_GPIO, enabled ? APP_STATUS_LED_ACTIVE_LEVEL : inactive_level());
}

static void status_led_task(void *arg)
{
    (void)arg;

    while (true) {
        if (!s_initialized || !s_enabled) {
            vTaskDelay(pdMS_TO_TICKS(STATUS_LED_STEADY_REFRESH_MS));
            continue;
        }

        if (s_provisioning_mode) {
            set_led_active(true);
            vTaskDelay(pdMS_TO_TICKS(STATUS_LED_FAST_BLINK_ON_MS));
            set_led_active(false);
            vTaskDelay(pdMS_TO_TICKS(STATUS_LED_FAST_BLINK_OFF_MS));
            continue;
        }

        switch (s_mode) {
        case STATUS_LED_MODE_LOW_POWER:
            set_led_active(false);
            vTaskDelay(pdMS_TO_TICKS(STATUS_LED_STEADY_REFRESH_MS));
            break;
        case STATUS_LED_MODE_MAINTENANCE:
            set_led_active(true);
            vTaskDelay(pdMS_TO_TICKS(STATUS_LED_SLOW_BLINK_ON_MS));
            set_led_active(false);
            vTaskDelay(pdMS_TO_TICKS(STATUS_LED_SLOW_BLINK_OFF_MS));
            break;
        case STATUS_LED_MODE_NORMAL:
        default:
            set_led_active(true);
            vTaskDelay(pdMS_TO_TICKS(STATUS_LED_STEADY_REFRESH_MS));
            break;
        }
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

    s_mode = device_profile_low_power_enabled() ? STATUS_LED_MODE_LOW_POWER : STATUS_LED_MODE_NORMAL;
    ret = gpio_set_level(
        APP_STATUS_LED_GPIO,
        s_mode == STATUS_LED_MODE_LOW_POWER ? inactive_level() : APP_STATUS_LED_ACTIVE_LEVEL
    );
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
    /* The status LED now indicates the long-lived mode, so publish pulses stay disabled. */
}

void status_led_set_mode(status_led_mode_t mode)
{
    if (!s_initialized || !s_enabled) {
        return;
    }

    switch (mode) {
    case STATUS_LED_MODE_LOW_POWER:
    case STATUS_LED_MODE_MAINTENANCE:
    case STATUS_LED_MODE_NORMAL:
        s_mode = mode;
        break;
    default:
        s_mode = STATUS_LED_MODE_NORMAL;
        break;
    }
}

void status_led_set_provisioning(bool enabled)
{
    if (!s_initialized || !s_enabled) {
        return;
    }

    s_provisioning_mode = enabled;
}
