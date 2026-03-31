#include "telemetry_app.h"

#include <inttypes.h>
#include <stdio.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
#include "sensor_bus.h"
#include "shtc3_sensor.h"

#define TAG "telemetry_app"

#define OLED_PAGE_COUNT 4

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

static bool s_oled_initialized = false;
static uint32_t s_oled_page_index = 0;

static const char *bmpx80_model_text(uint8_t chip_id)
{
    return (chip_id == BMP280_CHIP_ID) ? "BMP280" : "BMP180";
}

static void oled_format_value_line(char *buffer, size_t buffer_size, const char *label, float value, const char *suffix)
{
    snprintf(buffer, buffer_size, "%s %.1f%s", label, value, suffix != NULL ? suffix : "");
}

static void oled_draw_status_line(int y, const char *label, bool ready)
{
    char line[20];
    snprintf(line, sizeof(line), "%s %s", label, ready ? "OK" : "ERR");
    oled_ssd1306_draw_text(0, y, line);
}

static void oled_try_init(void)
{
    if (s_oled_initialized || !sensor_bus_is_ready()) {
        return;
    }

    oled_ssd1306_config_t cfg = {
        .i2c_port = sensor_bus_i2c_port(),
        .sda_gpio = sensor_bus_i2c_sda_gpio(),
        .scl_gpio = sensor_bus_i2c_scl_gpio(),
        .pixel_clock_hz = APP_I2C_CLOCK_HZ,
        .primary_addr = 0x3C,
        .secondary_addr = 0x3D,
    };

    if (oled_ssd1306_init(&cfg) == ESP_OK) {
        s_oled_initialized = true;
    }
}

static void oled_render_page_0(void)
{
    char line[24];
    oled_ssd1306_draw_text(0, 0, "PAGE 1/4");
    oled_draw_status_line(16, "WIFI", network_service_is_wifi_ready());
    oled_draw_status_line(28, "MQTT", network_service_is_mqtt_ready());
    snprintf(line, sizeof(line), "RSSI %d", network_service_get_rssi());
    oled_ssd1306_draw_text(0, 40, line);
    snprintf(line, sizeof(line), "OLED %02X", oled_ssd1306_get_address());
    oled_ssd1306_draw_text(0, 52, line);
}

static void oled_render_page_1(const oled_shtc3_state_t *shtc3_state)
{
    char line[24];
    oled_ssd1306_draw_text(0, 0, "PAGE 2/4");
    oled_ssd1306_draw_text(0, 12, "SHTC3");
    if (shtc3_state != NULL && shtc3_state->ready) {
        oled_format_value_line(line, sizeof(line), "TEMP", shtc3_state->temperature_c, "C");
        oled_ssd1306_draw_text(0, 28, line);
        oled_format_value_line(line, sizeof(line), "HUM", shtc3_state->humidity_pct, "");
        oled_ssd1306_draw_text(0, 44, line);
    } else {
        oled_ssd1306_draw_text(0, 32, "ERR");
    }
}

static void oled_render_page_2(const oled_bmpx80_state_t *bmp_state)
{
    char line[24];
    oled_ssd1306_draw_text(0, 0, "PAGE 3/4");
    oled_ssd1306_draw_text(0, 12, (bmp_state != NULL && bmp_state->chip_id == BMP280_CHIP_ID) ? "BMP280" : "BMP180");
    if (bmp_state != NULL && bmp_state->ready) {
        oled_format_value_line(line, sizeof(line), "TEMP", bmp_state->temperature_c, "C");
        oled_ssd1306_draw_text(0, 28, line);
        oled_format_value_line(line, sizeof(line), "PRES", bmp_state->pressure_hpa, "");
        oled_ssd1306_draw_text(0, 44, line);
    } else {
        oled_ssd1306_draw_text(0, 32, "ERR");
    }
}

static void oled_render_page_3(const oled_bh1750_state_t *bh1750_state)
{
    char line[24];
    oled_ssd1306_draw_text(0, 0, "PAGE 4/4");
    oled_ssd1306_draw_text(0, 12, "BH1750");
    if (bh1750_state != NULL && bh1750_state->ready) {
        oled_format_value_line(line, sizeof(line), "LUX", bh1750_state->illuminance_lux, "");
        oled_ssd1306_draw_text(0, 32, line);
    } else {
        oled_ssd1306_draw_text(0, 32, "ERR");
    }
}

static void oled_render_cycle(const oled_bh1750_state_t *bh1750_state, const oled_bmpx80_state_t *bmp_state, const oled_shtc3_state_t *shtc3_state)
{
    if (!s_oled_initialized || !oled_ssd1306_is_ready()) {
        return;
    }

    oled_ssd1306_clear();
    switch (s_oled_page_index % OLED_PAGE_COUNT) {
    case 0:
        oled_render_page_0();
        break;
    case 1:
        oled_render_page_1(shtc3_state);
        break;
    case 2:
        oled_render_page_2(bmp_state);
        break;
    default:
        oled_render_page_3(bh1750_state);
        break;
    }
    if (oled_ssd1306_present() == ESP_OK) {
        s_oled_page_index = (s_oled_page_index + 1U) % OLED_PAGE_COUNT;
    }
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

static cJSON *create_sensor_node(cJSON *sensors_obj, const char *sensor_type, bool ready)
{
    cJSON *node = cJSON_AddObjectToObject(sensors_obj, sensor_type);
    if (node != NULL) {
        cJSON_AddBoolToObject(node, "ready", ready);
    }
    return node;
}

static void add_dht11_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, float *primary_temp, float *primary_humidity)
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
        device_profile_update_dht11(true, sample.temperature_c, sample.humidity_pct);
        char details[96];
        snprintf(details, sizeof(details), "\"temperature\":%.1f,\"humidity\":%.1f", sample.temperature_c, sample.humidity_pct);
        emit_sensor_event("dht11", true, details);
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "dht11", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        device_profile_update_dht11(false, sample.temperature_c, sample.humidity_pct);
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("dht11", false, details);
    }
}

static void add_ds18b20_sensor(cJSON *sensors_obj, int *total_count, int *ready_count)
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
        char details[64];
        snprintf(details, sizeof(details), "\"temperature\":%.2f", sample.temperature_c);
        emit_sensor_event("ds18b20", true, details);
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "ds18b20", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
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
    oled_shtc3_state_t *oled_state
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

static void add_rain_sensor(cJSON *sensors_obj, int *total_count, int *ready_count)
{
    if (!device_profile_has_sensor("rain_sensor")) {
        return;
    }

    analog_sensor_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = analog_sensor_read_rain(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = create_sensor_node(sensors_obj, "rain_sensor", true);
        cJSON_AddNumberToObject(node, "raw", sample.raw_value);
        cJSON_AddNumberToObject(node, "percent", sample.percent);
        (*ready_count)++;
        char details[96];
        snprintf(details, sizeof(details), "\"raw\":%d,\"percent\":%.1f", sample.raw_value, sample.percent);
        emit_sensor_event("rain_sensor", true, details);
    } else {
        cJSON *node = create_sensor_node(sensors_obj, "rain_sensor", false);
        cJSON_AddStringToObject(node, "reason", esp_err_to_name(ret));
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("rain_sensor", false, details);
    }
}

void telemetry_app_run(void)
{
    char payload[1536];
    char event_json[1792];
    const char *topic = NULL;

    dht11_sensor_init(APP_DHT11_GPIO);
    ds18b20_sensor_init(APP_DS18B20_GPIO);
    sensor_bus_init();
    analog_sensor_init();
    oled_try_init();

    while (1) {
        int total_count = 0;
        int ready_count = 0;
        float primary_temp = 0.0f;
        float primary_humidity = 0.0f;
        oled_bh1750_state_t oled_bh1750 = {0};
        oled_bmpx80_state_t oled_bmpx80 = {0};
        oled_shtc3_state_t oled_shtc3 = {0};

        cJSON *sensors_obj = cJSON_CreateObject();
        add_dht11_sensor(sensors_obj, &total_count, &ready_count, &primary_temp, &primary_humidity);
        add_ds18b20_sensor(sensors_obj, &total_count, &ready_count);
        add_bh1750_sensor(sensors_obj, &total_count, &ready_count, &oled_bh1750);
        add_bmp180_sensor(sensors_obj, &total_count, &ready_count, &oled_bmpx80);
        add_shtc3_sensor(sensors_obj, &total_count, &ready_count, &primary_temp, &primary_humidity, &oled_shtc3);
        add_soil_sensor(sensors_obj, &total_count, &ready_count);
        add_rain_sensor(sensors_obj, &total_count, &ready_count);

        char *sensor_json = cJSON_PrintUnformatted(sensors_obj);
        device_profile_update_sensor_snapshot(sensor_json, ready_count, total_count);

        topic = device_profile_mqtt_topic();
        if (ready_count > 0 && network_service_is_wifi_ready() && network_service_is_mqtt_ready()) {
            snprintf(
                payload,
                sizeof(payload),
                "{\"device\":\"%s\",\"alias\":\"%s\",\"ts\":%" PRId64 ",\"fwVersion\":\"%s\",\"rssi\":%d,\"sensors\":%s}",
                device_profile_device_id(),
                device_profile_device_alias(),
                esp_timer_get_time() / 1000,
                APP_FIRMWARE_VERSION,
                network_service_get_rssi(),
                sensor_json != NULL ? sensor_json : "{}"
            );

            if (network_service_publish_json(topic, payload) == ESP_OK) {
                device_profile_update_publish(true, network_service_get_rssi(), payload);
                snprintf(
                    event_json,
                    sizeof(event_json),
                    "{\"ready\":true,\"topic\":\"%s\",\"payload\":%s}",
                    topic,
                    payload
                );
                console_service_emit_event("publish", event_json);
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
            console_service_emit_event("runtime", "{\"status\":\"waiting sensors/wifi/mqtt\"}");
        }

        oled_try_init();
        oled_render_cycle(&oled_bh1750, &oled_bmpx80, &oled_shtc3);

        cJSON_free(sensor_json);
        cJSON_Delete(sensors_obj);
        vTaskDelay(pdMS_TO_TICKS(APP_PUBLISH_INTERVAL_MS));
    }
}
