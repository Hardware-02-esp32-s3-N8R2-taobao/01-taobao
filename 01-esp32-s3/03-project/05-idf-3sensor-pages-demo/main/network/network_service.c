#include "network/network_service.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

#include "app/app_config.h"
#include "pump/pump_controller.h"

static esp_mqtt_client_handle_t s_mqtt_client;
static bool s_wifi_connected;
static bool s_mqtt_connected;
static int s_wifi_rssi_dbm = -127;
static int64_t s_last_gateway_ping_us;
static gateway_status_t s_gateway_status;
static char s_gateway_payload_buffer[768];
static char s_pump_payload_buffer[256];

bool network_service_configured(void)
{
    return strcmp(WIFI_SSID, "YOUR_WIFI_NAME") != 0 && strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0;
}

static bool mqtt_topic_matches(const char *topic, int topic_len, const char *expected)
{
    size_t expected_len = strlen(expected);
    return topic != NULL && topic_len == (int)expected_len && strncmp(topic, expected, expected_len) == 0;
}

static void refresh_wifi_rssi(void)
{
    wifi_ap_record_t ap_info = {0};

    if (!s_wifi_connected) {
        s_wifi_rssi_dbm = -127;
        return;
    }

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_wifi_rssi_dbm = ap_info.rssi;
    }
}

static void mqtt_publish_gateway_ping(void)
{
    char payload[96];

    if (!s_mqtt_connected || s_mqtt_client == NULL) {
        return;
    }

    snprintf(payload, sizeof(payload), "{\"device\":\"%s\",\"alias\":\"%s\"}", MQTT_GATEWAY_DEVICE, MQTT_DEVICE_ALIAS);
    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_GATEWAY_PING, payload, 0, 0, 0);
    s_last_gateway_ping_us = esp_timer_get_time();
}

static void maybe_publish_gateway_ping(void)
{
    int64_t now_us = esp_timer_get_time();

    if (!s_mqtt_connected || s_mqtt_client == NULL) {
        return;
    }

    if (now_us - s_last_gateway_ping_us < GATEWAY_PING_INTERVAL_US) {
        return;
    }

    mqtt_publish_gateway_ping();
}

static void gateway_status_update_from_json(const char *json_text)
{
    cJSON *root = cJSON_Parse(json_text);

    if (root == NULL) {
        ESP_LOGW(APP_TAG, "Gateway heartbeat JSON parse failed");
        return;
    }

    cJSON *server_online = cJSON_GetObjectItemCaseSensitive(root, "serverOnline");
    cJSON *mqtt_online = cJSON_GetObjectItemCaseSensitive(root, "mqttOnline");
    cJSON *http_online = cJSON_GetObjectItemCaseSensitive(root, "httpOnline");
    cJSON *public_url_available = cJSON_GetObjectItemCaseSensitive(root, "publicUrlAvailable");
    cJSON *cpu_temperature = cJSON_GetObjectItemCaseSensitive(root, "cpuTemperatureC");

    s_gateway_status.valid = true;
    s_gateway_status.server_online = cJSON_IsTrue(server_online);
    s_gateway_status.mqtt_online = cJSON_IsTrue(mqtt_online);
    s_gateway_status.http_online = cJSON_IsTrue(http_online);
    s_gateway_status.public_url_available = cJSON_IsTrue(public_url_available);
    s_gateway_status.cpu_temperature_c = cJSON_IsNumber(cpu_temperature) ? (float)cpu_temperature->valuedouble : 0.0f;
    s_gateway_status.updated_at_us = esp_timer_get_time();

    ESP_LOGI(
        APP_TAG,
        "Gateway heartbeat: server=%d mqtt=%d http=%d public=%d",
        s_gateway_status.server_online ? 1 : 0,
        s_gateway_status.mqtt_online ? 1 : 0,
        s_gateway_status.http_online ? 1 : 0,
        s_gateway_status.public_url_available ? 1 : 0
    );

    cJSON_Delete(root);
}

static bool gateway_status_is_fresh(void)
{
    return s_gateway_status.valid && (esp_timer_get_time() - s_gateway_status.updated_at_us) <= GATEWAY_STATUS_TIMEOUT_US;
}

static bool mqtt_copy_payload_fragment(char *buffer, size_t buffer_len, esp_mqtt_event_handle_t event)
{
    int copy_offset = event->current_data_offset;
    int total_len = event->total_data_len;
    int copy_len = event->data_len;

    if (total_len <= 0 || total_len >= (int)buffer_len) {
        ESP_LOGW(APP_TAG, "MQTT payload too large: %d", total_len);
        return false;
    }

    if (copy_offset == 0) {
        memset(buffer, 0, buffer_len);
    }

    if (copy_offset + copy_len > total_len) {
        copy_len = total_len - copy_offset;
    }

    memcpy(buffer + copy_offset, event->data, (size_t)copy_len);

    if (copy_offset + copy_len < total_len) {
        return false;
    }

    buffer[total_len] = '\0';
    return true;
}

static void pump_command_handle_json(const char *json_text)
{
    cJSON *root = cJSON_Parse(json_text);
    if (root == NULL) {
        ESP_LOGW(APP_TAG, "Pump command JSON parse failed");
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device");
    cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *duration_seconds = cJSON_GetObjectItemCaseSensitive(root, "durationSeconds");
    cJSON *requested_by = cJSON_GetObjectItemCaseSensitive(root, "requestedBy");
    cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");

    if (!cJSON_IsString(type) || strcmp(type->valuestring, "pump-command") != 0) {
        ESP_LOGW(APP_TAG, "Ignore pump command with invalid type");
        cJSON_Delete(root);
        return;
    }

    if (!cJSON_IsString(device) || strcmp(device->valuestring, MQTT_DEVICE_ID) != 0) {
        ESP_LOGW(APP_TAG, "Ignore pump command for other device");
        cJSON_Delete(root);
        return;
    }

    if (!cJSON_IsString(action)) {
        ESP_LOGW(APP_TAG, "Ignore unsupported pump action");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(action->valuestring, "start") == 0) {
        if (!cJSON_IsNumber(duration_seconds) || duration_seconds->valuedouble <= 0) {
            ESP_LOGW(APP_TAG, "Ignore pump command with invalid duration");
            cJSON_Delete(root);
            return;
        }

        pump_controller_start(
            (uint32_t)duration_seconds->valueint,
            cJSON_IsString(requested_by) ? requested_by->valuestring : "mqtt",
            cJSON_IsString(ts) ? ts->valuestring : ""
        );
    } else if (strcmp(action->valuestring, "stop") == 0) {
        pump_controller_stop(
            cJSON_IsString(requested_by) ? requested_by->valuestring : "mqtt",
            cJSON_IsString(ts) ? ts->valuestring : ""
        );
    } else {
        ESP_LOGW(APP_TAG, "Ignore unsupported pump action: %s", action->valuestring);
        cJSON_Delete(root);
        return;
    }

    cJSON_Delete(root);
}

static void mqtt_publish_pressure(const pressure_sample_t *sample)
{
    char payload[160];

    if (!s_mqtt_connected || !sample->ready) {
        return;
    }

    snprintf(
        payload,
        sizeof(payload),
        "{\"device\":\"%s\",\"alias\":\"%s\",\"temperature\":%.1f,\"pressure\":%.1f}",
        MQTT_DEVICE_ID,
        MQTT_DEVICE_ALIAS,
        sample->temperature_c,
        sample->pressure_hpa
    );
    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_BMP280, payload, 0, 0, 0);
}

static void mqtt_publish_ds18b20(const ds18b20_sample_t *sample)
{
    char payload[128];

    if (!s_mqtt_connected || !sample->ready) {
        return;
    }

    snprintf(payload, sizeof(payload), "{\"device\":\"%s\",\"alias\":\"%s\",\"temperature\":%.1f}",
             MQTT_DEVICE_ID, MQTT_DEVICE_ALIAS, sample->temperature_c);
    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_DS18B20, payload, 0, 0, 0);
}

static void mqtt_publish_dht11(const dht11_sample_t *sample)
{
    char payload[160];

    if (!s_mqtt_connected || !sample->ready) {
        return;
    }

    snprintf(
        payload,
        sizeof(payload),
        "{\"device\":\"%s\",\"alias\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f}",
        MQTT_DEVICE_ID,
        MQTT_DEVICE_ALIAS,
        sample->temperature_c,
        sample->humidity_pct
    );
    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_DHT11, payload, 0, 0, 0);
}

static void mqtt_publish_bh1750(const bh1750_sample_t *sample)
{
    char payload[128];

    if (!s_mqtt_connected || !sample->ready) {
        return;
    }

    snprintf(payload, sizeof(payload), "{\"device\":\"%s\",\"alias\":\"%s\",\"illuminance\":%.1f}",
             MQTT_DEVICE_ID, MQTT_DEVICE_ALIAS, sample->lux);
    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_BH1750, payload, 0, 0, 0);
}

static void mqtt_publish_soil_moisture(const soil_moisture_sample_t *sample)
{
    char payload[160];

    if (!s_mqtt_connected || !sample->ready) {
        return;
    }

    snprintf(
        payload,
        sizeof(payload),
        "{\"device\":\"%s\",\"alias\":\"%s\",\"moisture\":%.1f,\"adcRaw\":%d,\"voltage\":%.3f}",
        MQTT_DEVICE_ID,
        MQTT_DEVICE_ALIAS,
        sample->moisture_pct,
        sample->raw,
        sample->voltage_v
    );
    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_SOIL_MOISTURE, payload, 0, 0, 0);
}

static void mqtt_publish_rain_sensor(const rain_sensor_sample_t *sample)
{
    char payload[192];

    if (!s_mqtt_connected || !sample->ready) {
        return;
    }

    snprintf(
        payload,
        sizeof(payload),
        "{\"device\":\"%s\",\"alias\":\"%s\",\"isRaining\":%s,\"rainLevel\":%.1f,\"adcRaw\":%d,\"voltage\":%.3f}",
        MQTT_DEVICE_ID,
        MQTT_DEVICE_ALIAS,
        sample->is_raining ? "true" : "false",
        sample->rain_level_pct,
        sample->raw,
        sample->voltage_v
    );
    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_RAIN_SENSOR, payload, 0, 0, 0);
}

void network_service_publish_pump_state(const pump_state_t *pump)
{
    char payload[256];

    if (!s_mqtt_connected || pump == NULL) {
        return;
    }

    snprintf(
        payload,
        sizeof(payload),
        "{\"type\":\"pump-state\",\"device\":\"%s\",\"alias\":\"%s\",\"active\":%s,"
        "\"commandReceived\":%s,\"remainingSeconds\":%lu,\"durationSeconds\":%lu,"
        "\"requestedBy\":\"%s\",\"issuedAt\":\"%s\"}",
        MQTT_DEVICE_ID,
        MQTT_DEVICE_ALIAS,
        pump->active ? "true" : "false",
        pump->command_received ? "true" : "false",
        (unsigned long)pump->remaining_seconds,
        (unsigned long)pump->duration_seconds,
        pump->requested_by,
        pump->issued_at
    );
    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_PUMP_STATUS, payload, 0, 0, 0);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_connected = true;
        ESP_LOGI(APP_TAG, "MQTT connected: %s", MQTT_BROKER_URI);
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_GATEWAY_STATUS, 0);
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_GATEWAY_BROADCAST, 0);
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_PUMP_SET, 0);
        mqtt_publish_gateway_ping();
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        ESP_LOGW(APP_TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_ERROR:
        s_mqtt_connected = false;
        ESP_LOGW(APP_TAG, "MQTT error");
        break;
    case MQTT_EVENT_DATA:
        if (
            mqtt_topic_matches(event->topic, event->topic_len, MQTT_TOPIC_GATEWAY_STATUS) ||
            mqtt_topic_matches(event->topic, event->topic_len, MQTT_TOPIC_GATEWAY_BROADCAST)
        ) {
            if (mqtt_copy_payload_fragment(s_gateway_payload_buffer, sizeof(s_gateway_payload_buffer), event)) {
                gateway_status_update_from_json(s_gateway_payload_buffer);
            }
        } else if (mqtt_topic_matches(event->topic, event->topic_len, MQTT_TOPIC_PUMP_SET)) {
            if (mqtt_copy_payload_fragment(s_pump_payload_buffer, sizeof(s_pump_payload_buffer), event)) {
                pump_command_handle_json(s_pump_payload_buffer);
            }
        }
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = MQTT_CLIENT_ID,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        s_mqtt_connected = false;
        s_wifi_rssi_dbm = -127;
        ESP_LOGW(APP_TAG, "WiFi disconnected, retrying");
        esp_wifi_connect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        refresh_wifi_rssi();
        ESP_LOGI(APP_TAG, "WiFi connected");
        if (s_mqtt_client == NULL) {
            mqtt_start();
        }
    }
}

void network_service_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void network_service_tick(void)
{
    refresh_wifi_rssi();
    maybe_publish_gateway_ping();
}

void network_service_publish_samples(const app_samples_t *samples)
{
    mqtt_publish_pressure(&samples->pressure);
    mqtt_publish_ds18b20(&samples->ds18b20);
    mqtt_publish_dht11(&samples->dht11);
    mqtt_publish_bh1750(&samples->bh1750);
    mqtt_publish_soil_moisture(&samples->soil_moisture);
    mqtt_publish_rain_sensor(&samples->rain);
    network_service_publish_pump_state(&samples->pump);
}

void network_service_format_status(char *wifi_buffer, size_t wifi_len, char *server_buffer, size_t server_len)
{
    if (!s_wifi_connected) {
        snprintf(wifi_buffer, wifi_len, "WERR");
    } else if (s_wifi_rssi_dbm > -127 && s_wifi_rssi_dbm < 0) {
        snprintf(wifi_buffer, wifi_len, "%dDB", s_wifi_rssi_dbm);
    } else {
        snprintf(wifi_buffer, wifi_len, "WIFI");
    }

    if (!s_wifi_connected || !s_mqtt_connected || !gateway_status_is_fresh()) {
        snprintf(server_buffer, server_len, "NG");
    } else if (s_gateway_status.server_online && s_gateway_status.mqtt_online) {
        snprintf(server_buffer, server_len, "OK");
    } else {
        snprintf(server_buffer, server_len, "NG");
    }
}
