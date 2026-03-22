#include "telemetry_app.h"

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "console_service.h"
#include "device_profile.h"
#include "dht11_sensor.h"
#include "network_service.h"

#define TAG "telemetry_app"

void telemetry_app_run(void)
{
    char payload[512];
    char event_json[768];
    const char *topic = NULL;
    dht11_sample_t sample = {0};

    dht11_sensor_init(APP_DHT11_GPIO);
    while (1) {
        esp_err_t dht_ret = dht11_sensor_read(&sample);
        if (dht_ret != ESP_OK) {
            device_profile_update_dht11(false, sample.temperature_c, sample.humidity_pct);
            snprintf(
                event_json,
                sizeof(event_json),
                "{\"sensorType\":\"dht11\",\"ready\":false,\"reason\":\"%s\"}",
                esp_err_to_name(dht_ret)
            );
            console_service_emit_event("sensor", event_json);
        } else {
            device_profile_update_dht11(sample.ready, sample.temperature_c, sample.humidity_pct);
            snprintf(
                event_json,
                sizeof(event_json),
                "{\"sensorType\":\"dht11\",\"ready\":true,\"temperature\":%.1f,\"humidity\":%.1f}",
                sample.temperature_c,
                sample.humidity_pct
            );
            console_service_emit_event("sensor", event_json);
        }

        topic = device_profile_mqtt_topic();
        if (sample.ready && network_service_is_wifi_ready() && network_service_is_mqtt_ready()) {
            snprintf(
                payload,
                sizeof(payload),
                "{\"device\":\"%s\",\"alias\":\"%s\",\"ts\":%" PRId64 ",\"fwVersion\":\"%s\",\"rssi\":%d,\"sensors\":{\"dht11\":{\"temperature\":%.1f,\"humidity\":%.1f}}}",
                device_profile_device_id(),
                device_profile_device_alias(),
                esp_timer_get_time() / 1000,
                APP_FIRMWARE_VERSION,
                network_service_get_rssi(),
                sample.temperature_c,
                sample.humidity_pct
            );

            if (network_service_publish_json(topic, payload) == ESP_OK) {
                device_profile_update_publish(
                    true,
                    sample.temperature_c,
                    sample.humidity_pct,
                    network_service_get_rssi(),
                    payload
                );
                snprintf(
                    event_json,
                    sizeof(event_json),
                    "{\"ready\":true,\"topic\":\"%s\",\"payload\":%s}",
                    topic,
                    payload
                );
                console_service_emit_event("publish", event_json);
            } else {
                device_profile_update_publish(false, sample.temperature_c, sample.humidity_pct, network_service_get_rssi(), payload);
                snprintf(
                    event_json,
                    sizeof(event_json),
                    "{\"ready\":false,\"topic\":\"%s\",\"reason\":\"mqtt not ready\"}",
                    topic
                );
                console_service_emit_event("publish", event_json);
            }
        } else {
            console_service_emit_event("runtime", "{\"status\":\"waiting dht11/wifi/mqtt\"}");
        }

        vTaskDelay(pdMS_TO_TICKS(APP_PUBLISH_INTERVAL_MS));
    }
}
