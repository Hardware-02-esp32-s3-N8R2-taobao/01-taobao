#include "telemetry_app.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_config.h"
#include "dht11_sensor.h"
#include "network_service.h"

#define TAG "telemetry_app"

void telemetry_app_run(void)
{
    char payload[256];
    dht11_sample_t sample = {0};

    dht11_sensor_init(APP_DHT11_GPIO);
    ESP_LOGI(TAG, "DHT11 data pin: GPIO%d", APP_DHT11_GPIO);

    while (1) {
        esp_err_t dht_ret = dht11_sensor_read(&sample);
        if (dht_ret != ESP_OK) {
            ESP_LOGW(TAG, "DHT11 read failed: %s", esp_err_to_name(dht_ret));
        } else {
            ESP_LOGI(
                TAG,
                "DHT11 sample: temperature=%.1f C humidity=%.1f %%RH",
                sample.temperature_c,
                sample.humidity_pct
            );
        }

        if (sample.ready && network_service_is_wifi_ready() && network_service_is_mqtt_ready()) {
            snprintf(
                payload,
                sizeof(payload),
                "{\"device\":\"%s\",\"alias\":\"%s\",\"source\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f,\"rssi\":%d,\"ip\":\"%s\"}",
                APP_DEVICE_ID,
                APP_DEVICE_ALIAS,
                APP_DEVICE_SOURCE,
                sample.temperature_c,
                sample.humidity_pct,
                network_service_get_rssi(),
                network_service_get_ip()
            );

            if (network_service_publish_json(APP_MQTT_TOPIC_TELEMETRY, payload) == ESP_OK) {
                ESP_LOGI(TAG, "publish ok: %s", payload);
            } else {
                ESP_LOGW(TAG, "publish skipped, mqtt not ready");
            }
        } else {
            ESP_LOGI(TAG, "waiting dht11/wifi/mqtt...");
        }

        vTaskDelay(pdMS_TO_TICKS(APP_PUBLISH_INTERVAL_MS));
    }
}
