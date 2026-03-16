#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "led_strip.h"

#define LED_GPIO 48
#define LED_COUNT 1
#define RMT_RESOLUTION_HZ (10 * 1000 * 1000)

static const char *TAG = "yd_demo";
static led_strip_handle_t s_led_strip;

typedef struct {
    const char *name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_color_t;

static const led_color_t s_colors[] = {
    { "red", 24, 0, 0 },
    { "green", 0, 24, 0 },
    { "blue", 0, 0, 24 },
    { "white", 18, 18, 18 },
};

static void configure_led(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = RMT_RESOLUTION_HZ,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_led_strip));
}

static void show_color(const led_color_t *color)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, color->red, color->green, color->blue));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

void app_main(void)
{
    ESP_LOGI(TAG, "YD-ESP32-S3 RGB demo starting");
    ESP_LOGI(TAG, "Board RGB LED is on GPIO%d", LED_GPIO);

    configure_led();

    while (1) {
        for (size_t i = 0; i < sizeof(s_colors) / sizeof(s_colors[0]); ++i) {
            ESP_LOGI(TAG, "Showing color: %s", s_colors[i].name);
            show_color(&s_colors[i]);
            vTaskDelay(pdMS_TO_TICKS(700));
        }
    }
}
