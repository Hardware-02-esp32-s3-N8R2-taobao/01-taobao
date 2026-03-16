#include "page_button/page_button.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "app/app_config.h"

static bool s_page_button_last_level = true;

void page_button_init(void)
{
    static const char *TAG = APP_TAG;

    const gpio_config_t button_conf = {
        .pin_bit_mask = 1ULL << PAGE_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&button_conf));
    s_page_button_last_level = gpio_get_level(PAGE_BUTTON_GPIO) != PAGE_BUTTON_ACTIVE;
    ESP_LOGI(TAG, "Page button on GPIO%d", PAGE_BUTTON_GPIO);
}

bool page_button_was_pressed(void)
{
    bool level_high = gpio_get_level(PAGE_BUTTON_GPIO) != PAGE_BUTTON_ACTIVE;
    bool pressed = s_page_button_last_level && !level_high;
    s_page_button_last_level = level_high;
    return pressed;
}
