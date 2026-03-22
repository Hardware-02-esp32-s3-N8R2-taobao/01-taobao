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
#include "bmp280_sensor.h"
#include "console_service.h"
#include "device_profile.h"
#include "dht11_sensor.h"
#include "ds18b20_sensor.h"
#include "network_service.h"
#include "sensor_bus.h"

#define TAG "telemetry_app"

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

static void add_dht11_sensor(cJSON *sensors_obj, int *total_count, int *ready_count, float *primary_temp, float *primary_humidity)
{
    if (!device_profile_has_sensor("dht11")) {
        return;
    }

    dht11_sample_t sample = {0};
    (*total_count)++;

    esp_err_t ret = dht11_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = cJSON_AddObjectToObject(sensors_obj, "dht11");
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
        cJSON *node = cJSON_AddObjectToObject(sensors_obj, "ds18b20");
        cJSON_AddNumberToObject(node, "temperature", sample.temperature_c);
        (*ready_count)++;
        char details[64];
        snprintf(details, sizeof(details), "\"temperature\":%.2f", sample.temperature_c);
        emit_sensor_event("ds18b20", true, details);
    } else {
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("ds18b20", false, details);
    }
}

static void add_bh1750_sensor(cJSON *sensors_obj, int *total_count, int *ready_count)
{
    if (!device_profile_has_sensor("bh1750")) {
        return;
    }

    bh1750_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = bh1750_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = cJSON_AddObjectToObject(sensors_obj, "bh1750");
        cJSON_AddNumberToObject(node, "illuminance", sample.illuminance_lux);
        cJSON_AddNumberToObject(node, "address", sample.address);
        (*ready_count)++;
        char details[96];
        snprintf(details, sizeof(details), "\"illuminance\":%.1f,\"address\":%u", sample.illuminance_lux, sample.address);
        emit_sensor_event("bh1750", true, details);
    } else {
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("bh1750", false, details);
    }
}

static void add_bmp280_sensor(cJSON *sensors_obj, int *total_count, int *ready_count)
{
    if (!device_profile_has_sensor("bmp280")) {
        return;
    }

    bmp280_sample_t sample = {0};
    (*total_count)++;
    esp_err_t ret = bmp280_sensor_read(&sample);
    if (ret == ESP_OK && sample.ready) {
        cJSON *node = cJSON_AddObjectToObject(sensors_obj, "bmp280");
        cJSON_AddNumberToObject(node, "temperature", sample.temperature_c);
        cJSON_AddNumberToObject(node, "pressure", sample.pressure_hpa);
        cJSON_AddNumberToObject(node, "address", sample.address);
        cJSON_AddNumberToObject(node, "chipId", sample.chip_id);
        cJSON_AddStringToObject(node, "model", sample.has_humidity ? "bme280" : "bmp280");
        if (sample.has_humidity) {
            cJSON_AddNumberToObject(node, "humidity", sample.humidity_pct);
        }
        (*ready_count)++;
        char details[192];
        if (sample.has_humidity) {
            snprintf(
                details,
                sizeof(details),
                "\"temperature\":%.2f,\"pressure\":%.2f,\"humidity\":%.2f,\"address\":%u,\"chipId\":%u,\"model\":\"bme280\"",
                sample.temperature_c,
                sample.pressure_hpa,
                sample.humidity_pct,
                sample.address,
                sample.chip_id
            );
        } else {
            snprintf(
                details,
                sizeof(details),
                "\"temperature\":%.2f,\"pressure\":%.2f,\"address\":%u,\"chipId\":%u,\"model\":\"bmp280\"",
                sample.temperature_c,
                sample.pressure_hpa,
                sample.address,
                sample.chip_id
            );
        }
        emit_sensor_event("bmp280", true, details);
    } else {
        char details[96];
        snprintf(details, sizeof(details), "\"reason\":\"%s\"", esp_err_to_name(ret));
        emit_sensor_event("bmp280", false, details);
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
        cJSON *node = cJSON_AddObjectToObject(sensors_obj, "soil_moisture");
        cJSON_AddNumberToObject(node, "raw", sample.raw_value);
        cJSON_AddNumberToObject(node, "percent", sample.percent);
        (*ready_count)++;
        char details[96];
        snprintf(details, sizeof(details), "\"raw\":%d,\"percent\":%.1f", sample.raw_value, sample.percent);
        emit_sensor_event("soil_moisture", true, details);
    } else {
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
        cJSON *node = cJSON_AddObjectToObject(sensors_obj, "rain_sensor");
        cJSON_AddNumberToObject(node, "raw", sample.raw_value);
        cJSON_AddNumberToObject(node, "percent", sample.percent);
        (*ready_count)++;
        char details[96];
        snprintf(details, sizeof(details), "\"raw\":%d,\"percent\":%.1f", sample.raw_value, sample.percent);
        emit_sensor_event("rain_sensor", true, details);
    } else {
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

    while (1) {
        int total_count = 0;
        int ready_count = 0;
        float primary_temp = 0.0f;
        float primary_humidity = 0.0f;

        cJSON *sensors_obj = cJSON_CreateObject();
        add_dht11_sensor(sensors_obj, &total_count, &ready_count, &primary_temp, &primary_humidity);
        add_ds18b20_sensor(sensors_obj, &total_count, &ready_count);
        add_bh1750_sensor(sensors_obj, &total_count, &ready_count);
        add_bmp280_sensor(sensors_obj, &total_count, &ready_count);
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

        cJSON_free(sensor_json);
        cJSON_Delete(sensors_obj);
        vTaskDelay(pdMS_TO_TICKS(APP_PUBLISH_INTERVAL_MS));
    }
}
