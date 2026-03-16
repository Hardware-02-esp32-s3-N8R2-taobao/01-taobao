#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_GPIO GPIO_NUM_2

static const char *TAG = "led_blink";

void app_main(void)
{
    bool led_on = false;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_LOGI(TAG, "GPIO2 LED blink demo starting");
    ESP_LOGI(TAG, "LED pin: GPIO%d", LED_GPIO);
    ESP_LOGI(TAG, "If this board uses active-low LED logic, visible state may be inverted");

    while (1) {
        gpio_set_level(LED_GPIO, led_on ? 1 : 0);
        ESP_LOGI(TAG, "LED %s", led_on ? "ON" : "OFF");
        led_on = !led_on;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
