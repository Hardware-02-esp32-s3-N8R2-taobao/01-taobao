#include "pump/pump_controller.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "app/app_config.h"
#include "board/board_support.h"

static const char *TAG = "pump";
static portMUX_TYPE s_pump_lock = portMUX_INITIALIZER_UNLOCKED;
static pump_state_t s_pump_state;

static uint32_t clamp_duration_seconds(uint32_t duration_seconds)
{
    if (duration_seconds == 0) {
        return 1;
    }

    if (duration_seconds > PUMP_MAX_DURATION_SECONDS) {
        return PUMP_MAX_DURATION_SECONDS;
    }

    return duration_seconds;
}

static uint32_t calculate_remaining_seconds_locked(int64_t now_us)
{
    if (!s_pump_state.active || s_pump_state.stop_at_us <= now_us) {
        return 0;
    }

    return (uint32_t)((s_pump_state.stop_at_us - now_us + 999999LL) / 1000000LL);
}

esp_err_t pump_controller_init(void)
{
    memset(&s_pump_state, 0, sizeof(s_pump_state));
    ESP_ERROR_CHECK(board_rgb_init());
    ESP_ERROR_CHECK(board_rgb_off());
    ESP_LOGI(TAG, "Pump simulator ready on board RGB LED");
    return ESP_OK;
}

void pump_controller_start(uint32_t duration_seconds, const char *requested_by, const char *issued_at)
{
    int64_t now_us = esp_timer_get_time();
    uint32_t clamped_duration = clamp_duration_seconds(duration_seconds);

    if (board_rgb_set(PUMP_SIM_COLOR_R, PUMP_SIM_COLOR_G, PUMP_SIM_COLOR_B) != ESP_OK) {
        ESP_LOGW(TAG, "Pump simulator RGB update failed");
    }

    portENTER_CRITICAL(&s_pump_lock);
    s_pump_state.command_received = true;
    s_pump_state.active = true;
    s_pump_state.duration_seconds = clamped_duration;
    s_pump_state.started_at_us = now_us;
    s_pump_state.stop_at_us = now_us + (int64_t)clamped_duration * 1000000LL;
    s_pump_state.remaining_seconds = clamped_duration;
    strlcpy(s_pump_state.requested_by, requested_by != NULL ? requested_by : "mqtt", sizeof(s_pump_state.requested_by));
    strlcpy(s_pump_state.issued_at, issued_at != NULL ? issued_at : "", sizeof(s_pump_state.issued_at));
    portEXIT_CRITICAL(&s_pump_lock);

    ESP_LOGI(TAG, "Pump command accepted: %lus by %s", (unsigned long)clamped_duration, s_pump_state.requested_by);
}

void pump_controller_stop(const char *requested_by, const char *issued_at)
{
    if (board_rgb_off() != ESP_OK) {
        ESP_LOGW(TAG, "Pump simulator RGB off failed");
    }

    portENTER_CRITICAL(&s_pump_lock);
    s_pump_state.command_received = true;
    s_pump_state.active = false;
    s_pump_state.remaining_seconds = 0;
    s_pump_state.stop_at_us = esp_timer_get_time();
    strlcpy(s_pump_state.requested_by, requested_by != NULL ? requested_by : "mqtt", sizeof(s_pump_state.requested_by));
    strlcpy(s_pump_state.issued_at, issued_at != NULL ? issued_at : "", sizeof(s_pump_state.issued_at));
    portEXIT_CRITICAL(&s_pump_lock);

    ESP_LOGI(TAG, "Pump stop command accepted by %s", s_pump_state.requested_by);
}

void pump_controller_tick(void)
{
    bool should_turn_off = false;
    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_pump_lock);
    if (s_pump_state.active) {
        s_pump_state.remaining_seconds = calculate_remaining_seconds_locked(now_us);
        if (s_pump_state.remaining_seconds == 0) {
            s_pump_state.active = false;
            should_turn_off = true;
        }
    }
    portEXIT_CRITICAL(&s_pump_lock);

    if (should_turn_off) {
        if (board_rgb_off() != ESP_OK) {
            ESP_LOGW(TAG, "Pump simulator RGB off failed");
        }
        ESP_LOGI(TAG, "Pump command completed");
    }
}

void pump_controller_get_state(pump_state_t *state)
{
    if (state == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_pump_lock);
    *state = s_pump_state;
    if (state->active) {
        state->remaining_seconds = calculate_remaining_seconds_locked(esp_timer_get_time());
    }
    portEXIT_CRITICAL(&s_pump_lock);
}
