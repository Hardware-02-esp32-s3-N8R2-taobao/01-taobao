#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_config.h"
#include "led_driver.h"
#include "oled_ssd1306.h"
#include "ui_status.h"

#define TAG "c3_led_oled"

void app_main(void)
{
    bool led_on = false;
    uint32_t blink_count = 0;

    const led_driver_config_t led_cfg = {
        .gpio_num = APP_LED_GPIO,
        .active_level = APP_LED_ACTIVE_LEVEL,
    };

    const oled_ssd1306_config_t oled_cfg = {
        .i2c_port = APP_I2C_HOST,
        .sda_gpio = APP_OLED_I2C_SDA_GPIO,
        .scl_gpio = APP_OLED_I2C_SCL_GPIO,
        .pixel_clock_hz = APP_OLED_PIXEL_CLOCK_HZ,
        .primary_addr = APP_OLED_I2C_ADDR_PRIMARY,
        .secondary_addr = APP_OLED_I2C_ADDR_SECONDARY,
    };

    ESP_ERROR_CHECK(led_driver_init(&led_cfg));

    ESP_LOGI(TAG, "ESP32-C3 SuperMini LED + OLED demo start");
    ESP_LOGI(TAG, "LED pin: GPIO%d, active level: %d", APP_LED_GPIO, APP_LED_ACTIVE_LEVEL);
    ESP_LOGI(TAG, "OLED wiring: SDA=GPIO%d SCL=GPIO%d", APP_OLED_I2C_SDA_GPIO, APP_OLED_I2C_SCL_GPIO);

    esp_err_t oled_ret = oled_ssd1306_init(&oled_cfg);
    if (oled_ret == ESP_OK) {
        ESP_LOGI(TAG, "OLED detected at 0x%02X", oled_ssd1306_get_address());
        ui_status_render(led_on, blink_count);
        ESP_ERROR_CHECK(oled_ssd1306_present());
    } else {
        ESP_LOGW(TAG, "OLED init failed: %s", esp_err_to_name(oled_ret));
    }

    while (1) {
        led_on = !led_on;
        blink_count++;
        ESP_ERROR_CHECK(led_driver_set(led_on));

        ESP_LOGI(TAG, "blink=%lu led=%s", (unsigned long)blink_count, led_on ? "ON" : "OFF");

        if (oled_ssd1306_is_ready()) {
            ui_status_render(led_on, blink_count);
            ESP_ERROR_CHECK(oled_ssd1306_present());
        }

        vTaskDelay(pdMS_TO_TICKS(APP_BLINK_PERIOD_MS));
    }
}
