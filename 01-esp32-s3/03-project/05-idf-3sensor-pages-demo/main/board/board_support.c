#include "board/board_support.h"

#include "esp_log.h"
#include "led_strip.h"

#include "app/app_config.h"

void board_rgb_off(void)
{
    static const char *TAG = APP_TAG;
    led_strip_handle_t rgb_led = NULL;

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = RGB_LED_COUNT,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = RGB_RMT_RESOLUTION_HZ,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &rgb_led));
    ESP_ERROR_CHECK(led_strip_clear(rgb_led));
    ESP_LOGI(TAG, "Board RGB LED turned off on GPIO%d", RGB_LED_GPIO);
}
