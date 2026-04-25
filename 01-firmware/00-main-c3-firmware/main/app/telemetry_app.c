#include "telemetry_app.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "analog_sensor.h"
#include "app_config.h"
#include "bh1750_sensor.h"
#include "bmp180_sensor.h"
#include "console_service.h"
#include "device_profile.h"
#include "dht11_sensor.h"
#include "ds18b20_sensor.h"
#include "network_service.h"
#include "oled_ssd1306.h"
#include "ina226_sensor.h"
#include "max17043_sensor.h"
#include "ota_service.h"
#include "remote_config_service.h"
#include "sensor_bus.h"
#include "shtc3_sensor.h"
#include "status_led.h"

#define TAG "telemetry_app"

#define OLED_MAX_PAGE_COUNT 16
#define LOW_POWER_NETWORK_WAIT_MS 20000
#define LOW_POWER_SLEEP_SETTLE_MS 500
#define LOW_POWER_STATUS_FLUSH_MS 900
#define REMOTE_CONFIG_ACK_WAIT_MS 5000
#define OLED_BUTTON_POLL_MS 40
#define OLED_PAGE_BUTTON_DEBOUNCE_MS 180
#define OLED_PAGE_BUTTON_LONG_PRESS_MS 3000
#define OLED_HEADER_Y 0
#define OLED_BIG_LINE_Y1 14
#define OLED_BIG_LINE_Y2 34
#define OLED_SINGLE_VALUE_Y 22
#define OLED_FOOTER_Y 56
#define OLED_COMPACT_HEADER_Y 0
#define OLED_COMPACT_CONTENT_Y 9
#define OLED_COMPACT_WIDTH 64
#define OLED_COMPACT_HEIGHT 32

RTC_DATA_ATTR static uint32_t s_rtc_force_low_power_claim = 0;

static TaskHandle_t s_telemetry_task_handle = NULL;
static bool s_server_maintenance_mode = false;
static char s_pending_server_mode_request[24] = {0};
static volatile uint32_t s_oled_mode_toggle_requests = 0;
static volatile bool s_button_maintenance_request_latched = false;
static bool s_oled_initialized = false;
static uint32_t s_oled_page_index = 0;
static bool s_oled_button_initialized = false;
static bool s_oled_button_page_step_enabled = false;
static volatile uint32_t s_oled_page_step_requests = 0;
static volatile TickType_t s_oled_button_press_tick = 0;
static volatile TickType_t s_oled_last_activity_tick = 0;
static volatile bool s_oled_button_long_press_handled = false;
static TaskHandle_t s_oled_button_task_handle = NULL;
static int s_oled_button_last_level = 1;
static TickType_t s_oled_button_last_edge_tick = 0;
static bool s_oled_button_pressed = false;
static TickType_t s_boot_normal_mode_until_tick = 0;
static bool s_boot_low_power_entry_pending = false;
static bool s_skip_cycle_delay = false;

static void notify_telemetry_task(void)
{
    if (s_telemetry_task_handle != NULL) {
        xTaskNotifyGive(s_telemetry_task_handle);
    }
}

static bool boot_low_power_entry_pending(bool low_power_enabled)
{
    return low_power_enabled && s_boot_low_power_entry_pending;
}

static bool boot_normal_mode_window_active(bool low_power_enabled)
{
    return boot_low_power_entry_pending(low_power_enabled);
}

static bool boot_normal_mode_window_expired(bool low_power_enabled)
{
    (void)low_power_enabled;
    return false;
}

static status_led_mode_t resolve_status_led_mode(bool low_power_enabled)
{
    if (boot_normal_mode_window_active(low_power_enabled)) {
        return STATUS_LED_MODE_NORMAL;
    }
    if (s_server_maintenance_mode) {
        return STATUS_LED_MODE_MAINTENANCE;
    }
    return low_power_enabled ? STATUS_LED_MODE_LOW_POWER : STATUS_LED_MODE_NORMAL;
}

static void refresh_status_led_mode(bool low_power_enabled)
{
    status_led_set_mode(resolve_status_led_mode(low_power_enabled));
}

static const char *current_server_mode_text(bool low_power_enabled)
{
    if (boot_normal_mode_window_active(low_power_enabled)) {
        return "normal";
    }
    if (s_server_maintenance_mode) {
        return "maintenance";
    }
    return low_power_enabled ? "low_power" : "normal";
}

static const char *button_long_press_target_mode(bool low_power_enabled)
{
    if (!boot_low_power_entry_pending(low_power_enabled) || s_server_maintenance_mode) {
        return NULL;
    }
    return "maintenance";
}

static void latch_button_mode_request(const char *target_mode)
{
    if (target_mode == NULL || target_mode[0] == '\0') {
        return;
    }

    if (strcmp(target_mode, "maintenance") == 0) {
        s_button_maintenance_request_latched = true;
        s_rtc_force_low_power_claim = 0;
    }
    notify_telemetry_task();
}

static void consume_button_mode_toggle_requests(bool low_power_enabled)
{
    uint32_t requests = __atomic_exchange_n(&s_oled_mode_toggle_requests, 0, __ATOMIC_RELAXED);
    bool latched_request = __atomic_exchange_n(&s_button_maintenance_request_latched, false, __ATOMIC_RELAXED);
    if (requests == 0 && !latched_request) {
        return;
    }

    char event_json[192];
    const char *target_mode = button_long_press_target_mode(low_power_enabled);
    if (target_mode == NULL) {
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"source\":\"button_long_press\",\"queued\":false,\"currentMode\":\"%s\",\"reason\":\"pre_sleep_boot_required\"}",
            current_server_mode_text(low_power_enabled)
        );
        console_service_emit_event("mode_request", event_json);
        return;
    }

    if (strcmp(s_pending_server_mode_request, target_mode) == 0) {
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"source\":\"button_long_press\",\"queued\":false,\"currentMode\":\"%s\",\"targetMode\":\"%s\",\"reason\":\"already_queued\"}",
            current_server_mode_text(low_power_enabled),
            target_mode
        );
        console_service_emit_event("mode_request", event_json);
        return;
    }

    snprintf(s_pending_server_mode_request, sizeof(s_pending_server_mode_request), "%s", target_mode);
    snprintf(
        event_json,
        sizeof(event_json),
        "{\"source\":\"button_long_press\",\"queued\":true,\"currentMode\":\"%s\",\"targetMode\":\"%s\"}",
        current_server_mode_text(low_power_enabled),
        s_pending_server_mode_request
    );
    console_service_emit_event("mode_request", event_json);
}

static void clear_pending_mode_request_if_applied(const remote_config_ack_t *ack)
{
    if (
        ack == NULL ||
        s_pending_server_mode_request[0] == '\0' ||
        ack->server_mode[0] == '\0' ||
        strcmp(ack->server_mode, s_pending_server_mode_request) != 0
    ) {
        return;
    }

    char event_json[192];
    snprintf(
        event_json,
        sizeof(event_json),
        "{\"source\":\"button_long_press\",\"queued\":false,\"targetMode\":\"%s\",\"applied\":true}",
        s_pending_server_mode_request
    );
    if (strcmp(s_pending_server_mode_request, "maintenance") == 0) {
        s_boot_low_power_entry_pending = false;
        s_button_maintenance_request_latched = false;
    }
    s_pending_server_mode_request[0] = '\0';
    console_service_emit_event("mode_request", event_json);
}

typedef struct {
    bool ready;
    float illuminance_lux;
} oled_bh1750_state_t;

typedef struct {
    bool ready;
    float temperature_c;
    float pressure_hpa;
    uint8_t chip_id;
} oled_bmpx80_state_t;

typedef struct {
    bool ready;
    float temperature_c;
    float humidity_pct;
} oled_shtc3_state_t;

typedef struct {
    bool configured;
    bool ready;
    float temperature_c;
    float humidity_pct;
    const char *source;
} oled_env_state_t;

typedef struct {
    bool configured;
    bool ready;
    float temperature_c;
} oled_ds18b20_state_t;

typedef struct {
    bool ready;
    float voltage_v;
    float percent;
} oled_battery_state_t;

typedef struct {
    bool ready;
    float voltage_v;
    float percent;
    uint8_t address;
} oled_max17043_state_t;

typedef struct {
    bool ready;
    float bus_voltage_v;
    float current_ma;
    float power_mw;
    uint8_t address;
} oled_ina226_state_t;

typedef enum {
    OLED_PAGE_TEMP = 0,
    OLED_PAGE_HUMIDITY,
    OLED_PAGE_NETWORK,
    OLED_PAGE_SIGNAL,
    OLED_PAGE_VERSION,
} oled_page_kind_t;

typedef struct {
    oled_page_kind_t kind;
} oled_page_entry_t;

typedef struct {
    bool publish_ready;
    int ready_count;
    int total_count;
    bool has_bh1750;
    bool has_bmp180;
    bool has_battery;
    bool has_max17043;
    bool has_ina226;
    oled_env_state_t env;
    oled_ds18b20_state_t ds18b20;
    oled_bh1750_state_t bh1750;
    oled_bmpx80_state_t bmpx80;
    oled_battery_state_t battery;
    oled_max17043_state_t max17043;
    oled_ina226_state_t ina226;
} oled_dashboard_state_t;

static const char *bmpx80_model_text(uint8_t chip_id)
{
    return (chip_id == BMP280_CHIP_ID) ? "BMP280" : "BMP180";
}

static bool oled_is_compact_layout(void)
{
    return device_profile_hardware_variant() == DEVICE_HW_VARIANT_OLED_SCREEN;
}

static bool oled_should_show_boot_screen(void)
{
    return oled_is_compact_layout() && esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
}

static bool low_power_button_supported(void)
{
    device_hw_variant_t variant = device_profile_hardware_variant();
    return variant == DEVICE_HW_VARIANT_OLED_SCREEN || variant == DEVICE_HW_VARIANT_SUPERMINI;
}

static bool low_power_button_should_step_pages(void)
{
    return device_profile_hardware_variant() == DEVICE_HW_VARIANT_OLED_SCREEN;
}

static bool low_power_button_should_wake_from_sleep(void)
{
    return device_profile_hardware_variant() == DEVICE_HW_VARIANT_SUPERMINI;
}

static void oled_note_activity(TickType_t now)
{
    s_oled_last_activity_tick = now;
}

static void sample_oled_button_state(void)
{
    if (!s_oled_button_initialized) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    int level = gpio_get_level(APP_SCREEN_PAGE_BUTTON_GPIO);
    if (level != s_oled_button_last_level) {
        s_oled_button_last_level = level;
        s_oled_button_last_edge_tick = now;
        return;
    }

    if ((now - s_oled_button_last_edge_tick) < pdMS_TO_TICKS(OLED_PAGE_BUTTON_DEBOUNCE_MS)) {
        return;
    }

    bool active = level == APP_SCREEN_PAGE_BUTTON_ACTIVE_LEVEL;
    if (active != s_oled_button_pressed) {
        s_oled_button_pressed = active;
        oled_note_activity(now);
        if (active) {
            s_oled_button_press_tick = now;
            s_oled_button_long_press_handled = false;
            return;
        }

        TickType_t pressed_at = s_oled_button_press_tick;
        bool long_press_already_handled = s_oled_button_long_press_handled;
        bool long_press_triggered =
            long_press_already_handled ||
            (pressed_at != 0 && (now - pressed_at) >= pdMS_TO_TICKS(OLED_PAGE_BUTTON_LONG_PRESS_MS));
        s_oled_button_press_tick = 0;
        s_oled_button_long_press_handled = false;
        if (long_press_triggered) {
            if (!long_press_already_handled) {
                s_oled_mode_toggle_requests++;
                latch_button_mode_request("maintenance");
            }
            notify_telemetry_task();
        } else if (s_oled_button_page_step_enabled) {
            s_oled_page_step_requests++;
            notify_telemetry_task();
        }
        return;
    }

    if (
        s_oled_button_pressed &&
        s_oled_button_press_tick != 0 &&
        !s_oled_button_long_press_handled &&
        (now - s_oled_button_press_tick) >= pdMS_TO_TICKS(OLED_PAGE_BUTTON_LONG_PRESS_MS)
    ) {
        s_oled_button_long_press_handled = true;
        s_oled_mode_toggle_requests++;
        latch_button_mode_request("maintenance");
        notify_telemetry_task();
    }
}

static void oled_button_task(void *arg)
{
    (void)arg;
    while (1) {
        sample_oled_button_state();
        vTaskDelay(pdMS_TO_TICKS(OLED_BUTTON_POLL_MS));
    }
}

static void oled_upper_copy(char *buffer, size_t buffer_size, const char *text)
{
    if (buffer_size == 0) {
        return;
    }

    size_t index = 0;
    if (text != NULL) {
        while (text[index] != '\0' && index + 1 < buffer_size) {
            char ch = text[index];
            if (ch >= 'a' && ch <= 'z') {
                ch = (char)(ch - 'a' + 'A');
            }
            buffer[index] = ch;
            index++;
        }
    }
    buffer[index] = '\0';
}

static void oled_draw_small_line(int x, int y, const char *text)
{
    char line[32];
    oled_upper_copy(line, sizeof(line), text);
    oled_ssd1306_draw_text(x, y, line);
}

static void oled_draw_small_bold_line(int x, int y, const char *text)
{
    char line[32];
    oled_upper_copy(line, sizeof(line), text);
    oled_ssd1306_draw_text_scaled_bold(x, y, line, 1);
}

static int oled_text_width_px(const char *text, uint8_t scale)
{
    size_t len = text != NULL ? strlen(text) : 0;
    uint8_t actual_scale = scale == 0 ? 1 : scale;
    return (int)(len * 6U * actual_scale);
}

static void oled_draw_compact_header(size_t page_index, size_t page_count)
{
    char line[16];
    snprintf(line, sizeof(line), "%u/%u", (unsigned)(page_index + 1), (unsigned)page_count);
    oled_draw_small_line(0, OLED_COMPACT_HEADER_Y, line);
}

static void oled_draw_compact_content(const char *text)
{
    char line[24];

    oled_upper_copy(line, sizeof(line), text != NULL ? text : "--");
    oled_ssd1306_draw_text_scaled(0, OLED_COMPACT_CONTENT_Y, line, 1);
}

static void oled_render_boot_screen(void)
{
    if (!oled_should_show_boot_screen() || !oled_ssd1306_is_ready()) {
        return;
    }

    oled_ssd1306_clear();
    oled_draw_small_line(0, 0, "BOOT");
    oled_draw_small_line(0, 10, APP_FIRMWARE_VERSION);
    (void)oled_ssd1306_set_display_enabled(true);
    (void)oled_ssd1306_present();
    vTaskDelay(pdMS_TO_TICKS(1200));
}

static void oled_draw_header(const char *title, size_t page_index, size_t page_count)
{
    if (oled_is_compact_layout()) {
        oled_draw_compact_header(page_index, page_count);
        return;
    }

    char line[24];
    snprintf(line, sizeof(line), "%u/%u %s", (unsigned)(page_index + 1), (unsigned)page_count, title);
    oled_draw_small_line(0, OLED_HEADER_Y, line);
}

static void oled_draw_footer(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    oled_draw_small_line(0, OLED_FOOTER_Y, text);
}

static void oled_render_value_page(const char *title, const char *value, const char *footer, size_t page_index, size_t page_count)
{
    if (oled_is_compact_layout()) {
        oled_draw_header(title, page_index, page_count);
        oled_draw_compact_content(value);
        return;
    }

    oled_draw_header(title, page_index, page_count);
    oled_ssd1306_draw_text_scaled(0, OLED_SINGLE_VALUE_Y, value != NULL ? value : "--", 2);
    oled_draw_footer(footer);
}

static void oled_render_dual_page(const char *title, const char *line1, const char *line2, const char *footer, size_t page_index, size_t page_count)
{
    if (oled_is_compact_layout()) {
        (void)line2;
        oled_draw_header(title, page_index, page_count);
        oled_draw_compact_content(line1);
        return;
    }

    oled_draw_header(title, page_index, page_count);
    oled_ssd1306_draw_text_scaled(0, OLED_BIG_LINE_Y1, line1 != NULL ? line1 : "--", 2);
    oled_ssd1306_draw_text_scaled(0, OLED_BIG_LINE_Y2, line2 != NULL ? line2 : "--", 2);
    oled_draw_footer(footer);
}

static void oled_append_page(oled_page_entry_t *pages, size_t max_pages, size_t *page_count, oled_page_kind_t kind)
{
    if (*page_count >= max_pages) {
        return;
    }
    pages[*page_count].kind = kind;
    (*page_count)++;
}

static size_t oled_build_page_list(const oled_dashboard_state_t *state, oled_page_entry_t *pages, size_t max_pages)
{
    size_t page_count = 0;

    (void)state;
    oled_append_page(pages, max_pages, &page_count, OLED_PAGE_TEMP);
    oled_append_page(pages, max_pages, &page_count, OLED_PAGE_HUMIDITY);
    oled_append_page(pages, max_pages, &page_count, OLED_PAGE_NETWORK);
    oled_append_page(pages, max_pages, &page_count, OLED_PAGE_SIGNAL);
    oled_append_page(pages, max_pages, &page_count, OLED_PAGE_VERSION);
    return page_count;
}

static void oled_try_init(void)
{
    if (s_oled_initialized || !sensor_bus_is_ready()) {
        return;
    }

    bool compact = oled_is_compact_layout();

    oled_ssd1306_config_t cfg = {
        .i2c_port = sensor_bus_i2c_port(),
        .sda_gpio = sensor_bus_i2c_sda_gpio(),
        .scl_gpio = sensor_bus_i2c_scl_gpio(),
        .pixel_clock_hz = APP_I2C_CLOCK_HZ,
        .width = compact ? OLED_COMPACT_WIDTH : 128,
        .height = compact ? OLED_COMPACT_HEIGHT : 64,
        .column_offset = compact ? 32 : 0,
        .display_offset = 0,
        .primary_addr = 0x3C,
        .secondary_addr = 0x3D,
        .rotate_180 = false,
    };

    if (oled_ssd1306_init(&cfg) == ESP_OK) {
        s_oled_initialized = true;
    }
}

static void oled_page_button_try_init(void)
{
    if (s_oled_button_initialized || !low_power_button_supported()) {
        return;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << APP_SCREEN_PAGE_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&io_conf) != ESP_OK) {
        return;
    }

    oled_note_activity(xTaskGetTickCount());
    s_oled_button_page_step_enabled = low_power_button_should_step_pages();
    s_oled_button_last_level = gpio_get_level(APP_SCREEN_PAGE_BUTTON_GPIO);
    s_oled_button_last_edge_tick = xTaskGetTickCount();
    s_oled_button_pressed = false;
    s_oled_button_press_tick = 0;
    s_oled_button_long_press_handled = false;
    if (s_oled_button_task_handle == NULL) {
        xTaskCreate(oled_button_task, "oled_btn_poll", 2048, NULL, 4, &s_oled_button_task_handle);
    }
    s_oled_button_initialized = true;
}

static void oled_consume_page_requests(size_t page_count)
{
    uint32_t steps = __atomic_exchange_n(&s_oled_page_step_requests, 0, __ATOMIC_RELAXED);
    if (page_count == 0 || steps == 0) {
        return;
    }
    s_oled_page_index = (s_oled_page_index + (steps % page_count)) % page_count;
}

static void oled_render_temp_page(const oled_dashboard_state_t *state, size_t page_index, size_t page_count)
{
    char value[24];
    if (state->env.ready) {
        snprintf(value, sizeof(value), "%.1fC", state->env.temperature_c);
    } else {
        snprintf(value, sizeof(value), "--");
    }
    oled_render_value_page("TEMP", value, state->env.source, page_index, page_count);
}

static void oled_render_humidity_page(const oled_dashboard_state_t *state, size_t page_index, size_t page_count)
{
    char value[24];
    if (state->env.ready) {
        snprintf(value, sizeof(value), "%.1f%%", state->env.humidity_pct);
    } else {
        snprintf(value, sizeof(value), "--");
    }
    oled_render_value_page("HUM", value, state->env.source, page_index, page_count);
}

static void emit_sensor_event(const char *sensor_type, bool ready, const char *details_json);
static cJSON *create_sensor_node(cJSON *sensors_obj, const char *sensor_type, bool ready);
static void add_ina226_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, oled_ina226_state_t *oled_state);

static void oled_render_network_page(size_t page_index, size_t page_count)
{
    char value[24];
    snprintf(value, sizeof(value), "%s", network_service_is_mqtt_ready() ? "ONLINE" : "OFFLINE");
    oled_render_value_page("NET", value, "", page_index, page_count);
}

static void oled_render_signal_page(size_t page_index, size_t page_count)
{
    char value[24];
    if (network_service_is_wifi_ready()) {
        snprintf(value, sizeof(value), "%dDB", network_service_get_rssi());
    } else {
        snprintf(value, sizeof(value), "--");
    }
    oled_render_value_page("RSSI", value, "", page_index, page_count);
}

static void oled_render_version_page(size_t page_index, size_t page_count)
{
    char value[24];
    snprintf(value, sizeof(value), "%s", device_profile_firmware_version());
    oled_render_value_page("VER", value, device_profile_device_id(), page_index, page_count);
}

static void add_max17043_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, oled_max17043_state_t *oled_state)
{
    if (!device_profile_has_sensor("max17043")) {
        return;
    }

    max17043_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = max17043_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "max17043", true);
        cJSON_AddNumberToObject(node, "voltage", sample.voltage_v);
        cJSON_AddNumberToObject(node, "percent", sample.percent);
        cJSON_AddNumberToObject(node, "address", sample.address);
        cJSON_AddNumberToObject(node, "rawVcell", sample.raw_vcell);
        cJSON_AddNumberToObject(node, "rawSoc", sample.raw_soc);
        (*ready_count)++;
        char details[160];
        snprintf(
            details,
            sizeof(details),
            "\"voltage\":%.3f,\"percent\":%.2f,\"address\":%u,\"rawVcell\":%u,\"rawSoc\":%u",
            sample.voltage_v,
            sample.percent,
            sample.address,
            sample.raw_vcell,
            sample.raw_soc
        );
        emit_sensor_event("max17043", true, details);
        if (oled_state != NULL) {
            oled_state->ready = true;
            oled_state->voltage_v = sample.voltage_v;
            oled_state->percent = sample.percent;
            oled_state->address = sample.address;
        }
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "max17043", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        cJSON_AddNumberToObject(node, "address", APP_MAX17043_ADDR);
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\",\"address\":%u", esp_err_to_name(ret), APP_MAX17043_ADDR);
        emit_sensor_event("max17043", false, details);
        if (oled_state != NULL) {
            oled_state->ready = false;
            oled_state->voltage_v = 0.0f;
            oled_state->percent = 0.0f;
            oled_state->address = APP_MAX17043_ADDR;
        }
    }
}

static void add_ina226_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, oled_ina226_state_t *oled_state)
{
    if (!device_profile_has_sensor("ina226")) {
        return;
    }

    ina226_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = ina226_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "ina226", true);
        cJSON_AddNumberToObject(node, "busVoltage", sample.bus_voltage_v);
        cJSON_AddNumberToObject(node, "shuntVoltage", sample.shunt_voltage_v);
        cJSON_AddNumberToObject(node, "currentMa", sample.current_ma);
        cJSON_AddNumberToObject(node, "powerMw", sample.power_mw);
        cJSON_AddNumberToObject(node, "address", sample.address);
        cJSON_AddNumberToObject(node, "rawShuntVoltage", sample.raw_shunt_voltage);
        cJSON_AddNumberToObject(node, "rawBusVoltage", sample.raw_bus_voltage);
        cJSON_AddNumberToObject(node, "rawCurrent", sample.raw_current);
        cJSON_AddNumberToObject(node, "rawPower", sample.raw_power);
        (*ready_count)++;
        char details[256];
        snprintf(
            details,
            sizeof(details),
            "\"busVoltage\":%.4f,\"shuntVoltage\":%.6f,\"currentMa\":%.3f,\"powerMw\":%.3f,\"address\":%u,\"rawShuntVoltage\":%d,\"rawBusVoltage\":%u,\"rawCurrent\":%d,\"rawPower\":%u",
            sample.bus_voltage_v,
            sample.shunt_voltage_v,
            sample.current_ma,
            sample.power_mw,
            sample.address,
            sample.raw_shunt_voltage,
            sample.raw_bus_voltage,
            sample.raw_current,
            sample.raw_power
        );
        emit_sensor_event("ina226", true, details);
        if (oled_state != NULL) {
            oled_state->ready = true;
            oled_state->bus_voltage_v = sample.bus_voltage_v;
            oled_state->current_ma = sample.current_ma;
            oled_state->power_mw = sample.power_mw;
            oled_state->address = sample.address;
        }
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "ina226", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        cJSON_AddNumberToObject(node, "address", APP_INA226_ADDR);
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\",\"address\":%u", esp_err_to_name(ret), APP_INA226_ADDR);
        emit_sensor_event("ina226", false, details);
        if (oled_state != NULL) {
            oled_state->ready = false;
            oled_state->bus_voltage_v = 0.0f;
            oled_state->current_ma = 0.0f;
            oled_state->power_mw = 0.0f;
            oled_state->address = APP_INA226_ADDR;
        }
    }
}

static void oled_render_cycle(const oled_dashboard_state_t *state)
{
    oled_page_entry_t pages[OLED_MAX_PAGE_COUNT];
    size_t page_count = 0;

    if (!s_oled_initialized || !oled_ssd1306_is_ready()) {
        return;
    }

    page_count = oled_build_page_list(state, pages, OLED_MAX_PAGE_COUNT);
    if (page_count == 0) {
        return;
    }

    oled_consume_page_requests(page_count);
    s_oled_page_index %= page_count;
    oled_ssd1306_clear();

    switch (pages[s_oled_page_index].kind) {
    case OLED_PAGE_TEMP:
        oled_render_temp_page(state, s_oled_page_index, page_count);
        break;
    case OLED_PAGE_HUMIDITY:
        oled_render_humidity_page(state, s_oled_page_index, page_count);
        break;
    case OLED_PAGE_NETWORK:
        oled_render_network_page(s_oled_page_index, page_count);
        break;
    case OLED_PAGE_SIGNAL:
        oled_render_signal_page(s_oled_page_index, page_count);
        break;
    case OLED_PAGE_VERSION:
        oled_render_version_page(s_oled_page_index, page_count);
        break;
    default:
        break;
    }
    (void)oled_ssd1306_present();
}

static void emit_sensor_event(const char *sensor_type, bool ready, const char *details_json)
{
    char event_json[256];
    snprintf(
        event_json,
        sizeof(event_json),
        "{\"sensorType\":\"%s\",\"ready\":%s%s%s}",
        sensor_type,
        ready ? "true" : "false",
        (details_json != NULL && details_json[0] != '\0') ? "," : "",
        (details_json != NULL && details_json[0] != '\0') ? details_json : ""
    );
    console_service_emit_event("sensor", event_json);
}

static bool wait_for_network_ready(int timeout_ms)
{
    int waited_ms = 0;
    while (waited_ms < timeout_ms) {
        if (network_service_is_wifi_ready() && network_service_is_mqtt_ready()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        waited_ms += 200;
    }
    return network_service_is_wifi_ready() && network_service_is_mqtt_ready();
}

static void enter_low_power_sleep(void)
{
    uint64_t interval_us = (uint64_t)device_profile_low_power_interval_sec() * 1000000ULL;
    char event_json[160];

    s_boot_low_power_entry_pending = false;
    s_button_maintenance_request_latched = false;

    snprintf(
        event_json,
        sizeof(event_json),
        "{\"enabled\":true,\"intervalSec\":%" PRIu32 ",\"action\":\"sleep\"}",
        device_profile_low_power_interval_sec()
    );
    console_service_emit_event("low_power", event_json);
    vTaskDelay(pdMS_TO_TICKS(LOW_POWER_SLEEP_SETTLE_MS));
    if (oled_ssd1306_is_ready()) {
        (void)oled_ssd1306_set_display_enabled(false);
    }
    network_service_prepare_for_sleep();
    if (low_power_button_should_wake_from_sleep()) {
        gpio_config_t button_conf = {
            .pin_bit_mask = 1ULL << APP_SCREEN_PAGE_BUTTON_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = APP_SCREEN_PAGE_BUTTON_ACTIVE_LEVEL == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = APP_SCREEN_PAGE_BUTTON_ACTIVE_LEVEL == 0 ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t button_ret = gpio_config(&button_conf);
        if (button_ret == ESP_OK && esp_sleep_is_valid_wakeup_gpio(APP_SCREEN_PAGE_BUTTON_GPIO)) {
            esp_deepsleep_gpio_wake_up_mode_t wake_mode =
                APP_SCREEN_PAGE_BUTTON_ACTIVE_LEVEL == 0 ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH;
            button_ret = esp_deep_sleep_enable_gpio_wakeup(1ULL << APP_SCREEN_PAGE_BUTTON_GPIO, wake_mode);
        }
        if (button_ret != ESP_OK) {
            ESP_LOGW(TAG, "gpio9 wakeup enable failed: %s", esp_err_to_name(button_ret));
        }
    }
    esp_sleep_enable_timer_wakeup(interval_us);
    esp_deep_sleep_start();
}

static cJSON *create_sensor_node(cJSON *sensors_obj, const char *sensor_type, bool ready)
{
    cJSON *node = cJSON_AddObjectToObject(sensors_obj, sensor_type);
    if (node != NULL) {
        cJSON_AddBoolToObject(node, "ready", ready);
    }
    return node;
}

static void add_wifi_signal_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, bool wifi_ready)
{
    (*total_count)++;

    if (wifi_ready) {
        int rssi = network_service_get_rssi();
        cJSON *node = create_sensor_node(sensors_obj, "wifi_signal", true);
        cJSON_AddNumberToObject(node, "rssi", rssi);
        (*ready_count)++;
        char details[64];
        snprintf(details, sizeof(details), "\"rssi\":%d", rssi);
        emit_sensor_event("wifi_signal", true, details);
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "wifi_signal", false);
        cJSON_AddStringToObject(node, "reason", "wifi not connected");
        emit_sensor_event("wifi_signal", false, "\"reason\":\"wifi not connected\"");
    }
}

static void add_dht11_sensor(
    cJSON *sensors_obj,
    int *total_count,
    int *ready_count,
    float *primary_temp,
    float *primary_humidity,
    oled_env_state_t *oled_env_state
)
{
    if (!device_profile_has_sensor("dht11")) {
        return;
    }

    dht11_sample_t sample = {0};
    (*total_count)++;

    esp_err_t ret = dht11_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "dht11", true);
        cJSON_AddNumberToObject(node, "temperature", sample.temperature_c);
        cJSON_AddNumberToObject(node, "humidity", sample.humidity_pct);
        (*ready_count)++;
        *primary_temp = sample.temperature_c;
        *primary_humidity = sample.humidity_pct;
        if (oled_env_state != NULL) {
            oled_env_state->ready = true;
            oled_env_state->temperature_c = sample.temperature_c;
            oled_env_state->humidity_pct = sample.humidity_pct;
            oled_env_state->source = "DHT11";
        }
        device_profile_update_dht11(true, sample.temperature_c, sample.humidity_pct);
        char details[96];
        snprintf(details, sizeof(details), "\"temperature\":%.1f,\"humidity\":%.1f", sample.temperature_c, sample.humidity_pct);
        emit_sensor_event("dht11", true, details);
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "dht11", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        device_profile_update_dht11(false, sample.temperature_c, sample.humidity_pct);
        if (oled_env_state != NULL && oled_env_state->source == NULL) {
            oled_env_state->source = "DHT11";
        }
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("dht11", false, details);
    }
}

static void add_ds18b20_sensor(
    cJSON *sensors_obj,
    int *total_count,
    int *ready_count,
    oled_ds18b20_state_t *oled_state
)
{
    if (!device_profile_has_sensor("ds18b20")) {
        return;
    }

    ds18b20_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = ds18b20_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "ds18b20", true);
        cJSON_AddNumberToObject(node, "temperature", sample.temperature_c);
        (*ready_count)++;
        if (oled_state != NULL) {
            oled_state->ready = true;
            oled_state->temperature_c = sample.temperature_c;
        }
        char details[64];
        snprintf(details, sizeof(details), "\"temperature\":%.2f", sample.temperature_c);
        emit_sensor_event("ds18b20", true, details);
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "ds18b20", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        if (oled_state != NULL) {
            oled_state->ready = false;
            oled_state->temperature_c = 0.0f;
        }
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("ds18b20", false, details);
    }
}

static void add_bh1750_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, oled_bh1750_state_t *oled_state)
{
    if (!device_profile_has_sensor("bh1750")) {
        return;
    }

    bh1750_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = bh1750_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "bh1750", true);
        cJSON_AddNumberToObject(node, "illuminance", sample.illuminance_lux);
        cJSON_AddNumberToObject(node, "address", sample.address);
        (*ready_count)++;
        char details[96];
        snprintf(details, sizeof(details), "\"illuminance\":%.1f,\"address\":%u", sample.illuminance_lux, sample.address);
        emit_sensor_event("bh1750", true, details);
        if (oled_state != NULL) {
            oled_state->ready = true;
            oled_state->illuminance_lux = sample.illuminance_lux;
        }
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "bh1750", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("bh1750", false, details);
        if (oled_state != NULL) {
            oled_state->ready = false;
            oled_state->illuminance_lux = 0.0f;
        }
    }
}

static void add_bmp180_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, oled_bmpx80_state_t *oled_state)
{
    if (!device_profile_has_sensor("bmp180")) {
        return;
    }

    bmp180_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = bmp180_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "bmp180", true);
        cJSON_AddNumberToObject(node, "temperature", sample.temperature_c);
        cJSON_AddNumberToObject(node, "pressure", sample.pressure_hpa);
        cJSON_AddNumberToObject(node, "address", sample.address);
        cJSON_AddNumberToObject(node, "chipId", sample.chip_id);
        cJSON_AddStringToObject(node, "model", bmpx80_model_text(sample.chip_id));
        (*ready_count)++;
        char details[160];
        snprintf(
            details,
            sizeof(details),
            "\"temperature\":%.2f,\"pressure\":%.2f,\"address\":%u,\"chipId\":%u,\"model\":\"%s\"",
            sample.temperature_c,
            sample.pressure_hpa,
            sample.address,
            sample.chip_id,
            bmpx80_model_text(sample.chip_id)
        );
        emit_sensor_event("bmp180", true, details);
        if (oled_state != NULL) {
            oled_state->ready = true;
            oled_state->temperature_c = sample.temperature_c;
            oled_state->pressure_hpa = sample.pressure_hpa;
            oled_state->chip_id = sample.chip_id;
        }
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "bmp180", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("bmp180", false, details);
        if (oled_state != NULL) {
            oled_state->ready = false;
            oled_state->temperature_c = 0.0f;
            oled_state->pressure_hpa = 0.0f;
            oled_state->chip_id = 0;
        }
    }
}

static void add_shtc3_sensor(
    cJSON *sensors_obj,
    int *total_count,
    int *ready_count,
    float *primary_temp,
    float *primary_humidity,
    oled_shtc3_state_t *oled_state,
    oled_env_state_t *oled_env_state
)
{
    if (!device_profile_has_sensor("shtc3")) {
        return;
    }

    shtc3_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = shtc3_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "shtc3", true);
        cJSON_AddNumberToObject(node, "temperature", sample.temperature_c);
        cJSON_AddNumberToObject(node, "humidity", sample.humidity_pct);
        cJSON_AddNumberToObject(node, "address", sample.address);
        (*ready_count)++;
        *primary_temp = sample.temperature_c;
        *primary_humidity = sample.humidity_pct;
        if (oled_env_state != NULL) {
            oled_env_state->ready = true;
            oled_env_state->temperature_c = sample.temperature_c;
            oled_env_state->humidity_pct = sample.humidity_pct;
            oled_env_state->source = "SHTC3";
        }
        char details[128];
        snprintf(
            details,
            sizeof(details),
            "\"temperature\":%.2f,\"humidity\":%.2f,\"address\":%u",
            sample.temperature_c,
            sample.humidity_pct,
            sample.address
        );
        emit_sensor_event("shtc3", true, details);
        if (oled_state != NULL) {
            oled_state->ready = true;
            oled_state->temperature_c = sample.temperature_c;
            oled_state->humidity_pct = sample.humidity_pct;
        }
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "shtc3", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        cJSON_AddNumberToObject(node, "address", APP_SHTC3_ADDR);
        if (oled_env_state != NULL && oled_env_state->source == NULL) {
            oled_env_state->source = "SHTC3";
        }
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("shtc3", false, details);
        if (oled_state != NULL) {
            oled_state->ready = false;
            oled_state->temperature_c = 0.0f;
            oled_state->humidity_pct = 0.0f;
        }
    }
}

static void add_soil_sensor(cJSON *sensors_obj, int *total_count, int *ready_count)
{
    if (!device_profile_has_sensor("soil_moisture")) {
        return;
    }

    analog_sensor_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = analog_sensor_read_soil(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "soil_moisture", true);
        cJSON_AddNumberToObject(node, "raw", sample.raw_value);
        cJSON_AddNumberToObject(node, "percent", sample.percent);
        (*ready_count)++;
        char details[96];
        snprintf(details, sizeof(details), "\"raw\":%d,\"percent\":%.1f", sample.raw_value, sample.percent);
        emit_sensor_event("soil_moisture", true, details);
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "soil_moisture", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("soil_moisture", false, details);
    }
}

static void add_battery_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, oled_battery_state_t *oled_state)
{
    if (!device_profile_has_sensor("battery")) {
        return;
    }

    battery_voltage_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = analog_sensor_read_battery(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "battery", true);
        cJSON_AddNumberToObject(node, "voltage", sample.voltage_v);
        cJSON_AddNumberToObject(node, "percent", sample.percent);
        cJSON_AddNumberToObject(node, "raw", sample.raw_value);
        (*ready_count)++;
        char details[96];
        snprintf(details, sizeof(details), "\"voltage\":%.2f,\"percent\":%.1f,\"raw\":%d",
                 sample.voltage_v, sample.percent, sample.raw_value);
        emit_sensor_event("battery", true, details);
        if (oled_state != NULL) {
            oled_state->ready = true;
            oled_state->voltage_v = sample.voltage_v;
            oled_state->percent = sample.percent;
        }
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "battery", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("battery", false, details);
        if (oled_state != NULL) {
            oled_state->ready = false;
        }
    }
}

static bool sensor_enabled_in_csv(const char *enabled_sensors_csv, const char *sensor_key)
{
    if (enabled_sensors_csv == NULL || sensor_key == NULL || sensor_key[0] == '\0') {
        return false;
    }

    char local_copy[96];
    snprintf(local_copy, sizeof(local_copy), "%s", enabled_sensors_csv);

    char *token = strtok(local_copy, ",");
    while (token != NULL) {
        while (*token == ' ') {
            token++;
        }
        if (strcmp(token, sensor_key) == 0) {
            return true;
        }
        token = strtok(NULL, ",");
    }

    return false;
}

__attribute__((used)) static const char s_firmware_release_meta[] =
    "YDOTA_META:" APP_FIRMWARE_VERSION "|" APP_FIRMWARE_RELEASE_NOTES;

static void publish_runtime_snapshot(const char *topic, const char *sensor_json)
{
    char payload[2304];
    char config_json[512];
    char low_power_json[160];

    if (topic == NULL || topic[0] == '\0' || !network_service_is_mqtt_ready()) {
        return;
    }

    device_profile_build_config_json(config_json, sizeof(config_json));
    device_profile_build_low_power_json(low_power_json, sizeof(low_power_json));
    snprintf(
        payload,
        sizeof(payload),
        "{\"device\":\"%s\",\"alias\":\"%s\",\"ts\":%" PRId64 ",\"fwVersion\":\"%s\",\"rssi\":%d,\"config\":%s,\"lowPower\":%s,\"sensors\":%s}",
        device_profile_device_id(),
        device_profile_device_alias(),
        esp_timer_get_time() / 1000,
        device_profile_firmware_version(),
        network_service_get_rssi(),
        config_json,
        low_power_json,
        (sensor_json != NULL && sensor_json[0] != '\0') ? sensor_json : "{}"
    );

    if (network_service_publish_json(topic, payload) == ESP_OK) {
        device_profile_update_publish(true, network_service_get_rssi(), payload);
    }
}

static void publish_low_power_transition(const char *topic)
{
    publish_runtime_snapshot(topic, "{}");
    if (network_service_is_mqtt_ready()) {
        if (!network_service_wait_for_publish(LOW_POWER_STATUS_FLUSH_MS)) {
            vTaskDelay(pdMS_TO_TICKS(LOW_POWER_STATUS_FLUSH_MS));
        }
    }
}

void telemetry_app_run(void)
{
    char payload[2304];
    char event_json[2560];
    const char *topic = NULL;
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    dht11_sensor_init(APP_DHT11_GPIO);
    ds18b20_sensor_init(APP_DS18B20_GPIO);
    sensor_bus_init();
    analog_sensor_init();
    oled_try_init();
    s_telemetry_task_handle = xTaskGetCurrentTaskHandle();
    oled_page_button_try_init();
    sample_oled_button_state();
    oled_note_activity(xTaskGetTickCount());
    s_boot_low_power_entry_pending = false;
    s_button_maintenance_request_latched = false;
    if (device_profile_low_power_enabled() && wakeup_cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        s_boot_low_power_entry_pending = true;
        s_rtc_force_low_power_claim = 1;
        refresh_status_led_mode(true);
        console_service_emit_event(
            "low_power",
            "{\"enabled\":true,\"action\":\"stay_awake\",\"reason\":\"boot_wait_for_network_or_maintenance\"}"
        );
    }
    oled_render_boot_screen();
    ESP_LOGI(TAG, "firmware meta: %s", s_firmware_release_meta);

    while (1) {
        bool low_power_enabled = device_profile_low_power_enabled();
        if (!low_power_enabled) {
            s_boot_low_power_entry_pending = false;
            s_button_maintenance_request_latched = false;
        }
        bool publish_succeeded = false;
        bool server_ack_received = false;
        bool sleep_approved = false;
        bool config_job_completed = true;
        int total_count = 0;
        int ready_count = 0;
        float primary_temp = 0.0f;
        float primary_humidity = 0.0f;
        char publish_id[48] = {0};
        char remote_config_message[160] = {0};
        char sleep_reason[80] = {0};
        remote_config_ack_t remote_ack = {0};
        oled_env_state_t oled_env = {0};
        oled_bh1750_state_t oled_bh1750 = {0};
        oled_bmpx80_state_t oled_bmpx80 = {0};
        oled_shtc3_state_t oled_shtc3 = {0};
        oled_ds18b20_state_t oled_ds18b20 = {0};
        oled_battery_state_t oled_battery = {0};
        oled_max17043_state_t oled_max17043 = {0};
        oled_ina226_state_t oled_ina226 = {0};
        oled_dashboard_state_t oled_dashboard = {0};
        bool wifi_ready = network_service_is_wifi_ready();
        bool network_ready = wifi_ready && network_service_is_mqtt_ready();
        if (low_power_enabled && !network_ready) {
            network_ready = wait_for_network_ready(LOW_POWER_NETWORK_WAIT_MS);
        }
        wifi_ready = network_service_is_wifi_ready();
        if (wifi_ready) {
            ota_service_process();
        }
        sample_oled_button_state();
        low_power_enabled = device_profile_low_power_enabled();
        consume_button_mode_toggle_requests(low_power_enabled);
        refresh_status_led_mode(low_power_enabled);
        topic = device_profile_mqtt_topic();

        char enabled_sensors_csv[96] = {0};
        device_profile_copy_sensors_csv(enabled_sensors_csv, sizeof(enabled_sensors_csv));
        oled_env.configured =
            sensor_enabled_in_csv(enabled_sensors_csv, "dht11") ||
            sensor_enabled_in_csv(enabled_sensors_csv, "shtc3");
        oled_env.source = NULL;
        oled_ds18b20.configured = sensor_enabled_in_csv(enabled_sensors_csv, "ds18b20");
        oled_dashboard.has_bh1750 = sensor_enabled_in_csv(enabled_sensors_csv, "bh1750");
        oled_dashboard.has_bmp180 = sensor_enabled_in_csv(enabled_sensors_csv, "bmp180");
        oled_dashboard.has_battery = sensor_enabled_in_csv(enabled_sensors_csv, "battery");
        oled_dashboard.has_max17043 = sensor_enabled_in_csv(enabled_sensors_csv, "max17043");
        oled_dashboard.has_ina226 = sensor_enabled_in_csv(enabled_sensors_csv, "ina226");

        cJSON *sensors_obj = cJSON_CreateObject();
        if (sensor_enabled_in_csv(enabled_sensors_csv, "dht11")) {
            add_dht11_sensor(sensors_obj, &total_count, &ready_count, &primary_temp, &primary_humidity, &oled_env);
        }
        if (sensor_enabled_in_csv(enabled_sensors_csv, "ds18b20")) {
            add_ds18b20_sensor(sensors_obj, &total_count, &ready_count, &oled_ds18b20);
        }
        if (sensor_enabled_in_csv(enabled_sensors_csv, "bh1750")) {
            add_bh1750_sensor(sensors_obj, &total_count, &ready_count, &oled_bh1750);
        }
        if (sensor_enabled_in_csv(enabled_sensors_csv, "bmp180")) {
            add_bmp180_sensor(sensors_obj, &total_count, &ready_count, &oled_bmpx80);
        }
        if (sensor_enabled_in_csv(enabled_sensors_csv, "shtc3")) {
            add_shtc3_sensor(
                sensors_obj,
                &total_count,
                &ready_count,
                &primary_temp,
                &primary_humidity,
                &oled_shtc3,
                &oled_env
            );
        }
        if (sensor_enabled_in_csv(enabled_sensors_csv, "soil_moisture")) {
            add_soil_sensor(sensors_obj, &total_count, &ready_count);
        }
        if (sensor_enabled_in_csv(enabled_sensors_csv, "battery")) {
            add_battery_sensor(sensors_obj, &total_count, &ready_count, &oled_battery);
        }
        if (sensor_enabled_in_csv(enabled_sensors_csv, "max17043")) {
            add_max17043_sensor(sensors_obj, &total_count, &ready_count, &oled_max17043);
        }
        if (sensor_enabled_in_csv(enabled_sensors_csv, "ina226")) {
            add_ina226_sensor(sensors_obj, &total_count, &ready_count, &oled_ina226);
        }
        add_wifi_signal_sensor(sensors_obj, &total_count, &ready_count, wifi_ready);
        oled_dashboard.publish_ready = publish_succeeded;
        oled_dashboard.ready_count = ready_count;
        oled_dashboard.total_count = total_count;
        oled_dashboard.env = oled_env;
        oled_dashboard.ds18b20 = oled_ds18b20;
        oled_dashboard.bh1750 = oled_bh1750;
        oled_dashboard.bmpx80 = oled_bmpx80;
        oled_dashboard.battery = oled_battery;
        oled_dashboard.max17043 = oled_max17043;
        oled_dashboard.ina226 = oled_ina226;

        char *sensor_json = cJSON_PrintUnformatted(sensors_obj);
        device_profile_update_sensor_snapshot(sensor_json, ready_count, total_count);

        if (network_ready) {
            char config_json[512];
            char low_power_json[160];
            char mode_request_json[128] = {0};
            char force_low_power_json[160] = {0};

            device_profile_build_config_json(config_json, sizeof(config_json));
            device_profile_build_low_power_json(low_power_json, sizeof(low_power_json));
            if (s_pending_server_mode_request[0] != '\0') {
                snprintf(
                    mode_request_json,
                    sizeof(mode_request_json),
                    ",\"modeRequest\":{\"source\":\"button_long_press\",\"targetMode\":\"%s\"}",
                    s_pending_server_mode_request
                );
            }
            if (s_rtc_force_low_power_claim != 0) {
                snprintf(
                    force_low_power_json,
                    sizeof(force_low_power_json),
                    ",\"lowPowerResume\":{\"force\":true,\"source\":\"boot_normal_window_timeout\",\"intervalSec\":%" PRIu32 "}",
                    device_profile_low_power_interval_sec()
                );
            }
            snprintf(publish_id, sizeof(publish_id), "%lld", (long long)esp_timer_get_time());
            remote_config_service_prepare_for_publish(publish_id);
            snprintf(
                payload,
                sizeof(payload),
                "{\"device\":\"%s\",\"alias\":\"%s\",\"publishId\":\"%s\",\"ts\":%" PRId64 ",\"fwVersion\":\"%s\",\"rssi\":%d,\"config\":%s,\"lowPower\":%s,\"sensors\":%s%s%s}",
                device_profile_device_id(),
                device_profile_device_alias(),
                publish_id,
                esp_timer_get_time() / 1000,
                device_profile_firmware_version(),
                network_service_get_rssi(),
                config_json,
                low_power_json,
                sensor_json != NULL ? sensor_json : "{}",
                mode_request_json,
                force_low_power_json
            );

            if (network_service_publish_json(topic, payload) == ESP_OK) {
                publish_succeeded = true;
                device_profile_update_publish(true, network_service_get_rssi(), payload);
                status_led_blink_publish();
                snprintf(
                    event_json,
                    sizeof(event_json),
                    "{\"ready\":true,\"topic\":\"%s\",\"publishId\":\"%s\",\"payload\":%s}",
                    topic,
                    publish_id,
                    payload
                );
                console_service_emit_event("publish", event_json);
                if (remote_config_service_wait_for_ack(REMOTE_CONFIG_ACK_WAIT_MS, &remote_ack)) {
                    server_ack_received = true;
                    sleep_approved = remote_ack.sleep_approved;
                    s_server_maintenance_mode = strcmp(remote_ack.server_mode, "maintenance") == 0;
                    if (s_rtc_force_low_power_claim != 0 && strcmp(remote_ack.server_mode, "low_power") == 0) {
                        s_rtc_force_low_power_claim = 0;
                    }
                    clear_pending_mode_request_if_applied(&remote_ack);
                    snprintf(
                        sleep_reason,
                        sizeof(sleep_reason),
                        "%s",
                        remote_ack.sleep_reason[0] != '\0' ? remote_ack.sleep_reason : "server_ack"
                    );
                    refresh_status_led_mode(low_power_enabled);
                    if (remote_ack.has_job) {
                        config_job_completed = false;
                        esp_err_t config_err = remote_config_service_apply_ack_job(
                            &remote_ack,
                            remote_config_message,
                            sizeof(remote_config_message)
                        );
                        if (config_err == ESP_OK) {
                            config_job_completed = true;
                            low_power_enabled = device_profile_low_power_enabled();
                            refresh_status_led_mode(low_power_enabled);
                            topic = device_profile_mqtt_topic();
                            snprintf(
                                event_json,
                                sizeof(event_json),
                                "{\"publishId\":\"%s\",\"jobId\":\"%s\",\"ack\":true,\"sleepApproved\":%s,\"sleepReason\":\"%s\",\"applied\":true,\"message\":\"%s\"}",
                                publish_id,
                                remote_ack.job_id,
                                sleep_approved ? "true" : "false",
                                sleep_reason,
                                remote_config_message
                            );
                        } else {
                            snprintf(
                                event_json,
                                sizeof(event_json),
                                "{\"publishId\":\"%s\",\"jobId\":\"%s\",\"ack\":true,\"sleepApproved\":%s,\"sleepReason\":\"%s\",\"applied\":false,\"message\":\"%s\"}",
                                publish_id,
                                remote_ack.job_id,
                                sleep_approved ? "true" : "false",
                                sleep_reason,
                                remote_config_message
                            );
                        }
                    } else {
                        snprintf(
                            event_json,
                            sizeof(event_json),
                            "{\"publishId\":\"%s\",\"ack\":true,\"sleepApproved\":%s,\"sleepReason\":\"%s\",\"applied\":false,\"message\":\"no pending config job\"}",
                            publish_id,
                            sleep_approved ? "true" : "false",
                            sleep_reason
                        );
                    }
                    console_service_emit_event("remote_cfg", event_json);
                } else {
                    snprintf(
                        event_json,
                        sizeof(event_json),
                        "{\"publishId\":\"%s\",\"ack\":false,\"message\":\"server ack timeout\"}",
                        publish_id
                    );
                    console_service_emit_event("remote_cfg", event_json);
                }
            } else {
                device_profile_update_publish(false, network_service_get_rssi(), payload);
                snprintf(
                    event_json,
                    sizeof(event_json),
                    "{\"ready\":false,\"topic\":\"%s\",\"reason\":\"mqtt not ready\"}",
                    topic
                );
                console_service_emit_event("publish", event_json);
            }
        } else {
            console_service_emit_event("runtime", "{\"status\":\"waiting wifi/mqtt\"}");
        }

        oled_dashboard.publish_ready = publish_succeeded;
        oled_dashboard.ready_count = ready_count;
        oled_dashboard.total_count = total_count;

        oled_try_init();
        oled_page_button_try_init();
        bool boot_normal_active = boot_normal_mode_window_active(low_power_enabled);
        if (low_power_enabled && sleep_approved && !boot_normal_active) {
            if (oled_ssd1306_is_ready()) {
                (void)oled_ssd1306_set_display_enabled(false);
            }
        } else {
            if (oled_ssd1306_is_ready()) {
                (void)oled_ssd1306_set_display_enabled(true);
            }
            oled_render_cycle(&oled_dashboard);
        }

        cJSON_free(sensor_json);
        cJSON_Delete(sensors_obj);
        if (low_power_enabled && ota_service_should_skip_sleep()) {
            console_service_emit_event(
                "low_power",
                "{\"enabled\":true,\"action\":\"stay_awake\",\"reason\":\"ota_busy\"}"
            );
        } else if (low_power_enabled && boot_normal_active && !network_ready) {
            snprintf(
                event_json,
                sizeof(event_json),
                "{\"enabled\":true,\"action\":\"stay_awake\",\"reason\":\"boot_wait_for_network\"}"
            );
            console_service_emit_event("low_power", event_json);
        } else if (low_power_enabled && boot_normal_active && s_pending_server_mode_request[0] != '\0') {
            console_service_emit_event(
                "low_power",
                "{\"enabled\":true,\"action\":\"stay_awake\",\"reason\":\"boot_wait_for_maintenance_ack\"}"
            );
        } else if (low_power_enabled && publish_succeeded && server_ack_received && config_job_completed && sleep_approved) {
            enter_low_power_sleep();
        } else if (low_power_enabled && publish_succeeded && !server_ack_received) {
            console_service_emit_event(
                "low_power",
                "{\"enabled\":true,\"action\":\"stay_awake\",\"reason\":\"server_ack_timeout\"}"
            );
        } else if (low_power_enabled && publish_succeeded && !config_job_completed) {
            console_service_emit_event(
                "low_power",
                "{\"enabled\":true,\"action\":\"stay_awake\",\"reason\":\"config_job_not_confirmed\"}"
            );
        } else if (low_power_enabled && publish_succeeded && server_ack_received && !sleep_approved) {
            snprintf(
                event_json,
                sizeof(event_json),
                "{\"enabled\":true,\"action\":\"stay_awake\",\"reason\":\"%s\"}",
                sleep_reason[0] != '\0' ? sleep_reason : "server_denied_sleep"
            );
            console_service_emit_event("low_power", event_json);
        } else if (low_power_enabled) {
            console_service_emit_event(
                "low_power",
                "{\"enabled\":true,\"action\":\"stay_awake\",\"reason\":\"publish_not_ready\"}"
            );
        }
        TickType_t cycle_wait = s_skip_cycle_delay ? 0 : pdMS_TO_TICKS(APP_PUBLISH_INTERVAL_MS);
        s_skip_cycle_delay = false;
        ulTaskNotifyTake(pdTRUE, cycle_wait);
    }
}

void telemetry_app_request_immediate_cycle(void)
{
    if (s_telemetry_task_handle != NULL) {
        xTaskNotifyGive(s_telemetry_task_handle);
    }
}
