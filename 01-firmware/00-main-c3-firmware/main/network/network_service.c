#include "network_service.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "console_service.h"
#include "device_profile.h"

#define TAG "network_service"
#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1
#define WIFI_MAX_ENTRIES   8

typedef struct {
    char ssid[33];
    char password[65];
} ns_wifi_entry_t;

static EventGroupHandle_t    s_event_group;
static esp_mqtt_client_handle_t s_mqtt_client;
static esp_ip4_addr_t        s_ip_addr;
static int                   s_last_rssi = -127;

static ns_wifi_entry_t       s_wifi_list[WIFI_MAX_ENTRIES];
static int                   s_wifi_count = 0;
static int                   s_wifi_index = 0;
static bool                  s_reloading  = false;

/* ── WiFi list helpers ─────────────────────────────────────────────── */

static void load_wifi_list(void)
{
    s_wifi_count = device_profile_get_wifi_count();
    if (s_wifi_count > WIFI_MAX_ENTRIES) {
        s_wifi_count = WIFI_MAX_ENTRIES;
    }
    for (int i = 0; i < s_wifi_count; i++) {
        device_profile_get_wifi_entry(
            i,
            s_wifi_list[i].ssid,   sizeof(s_wifi_list[i].ssid),
            s_wifi_list[i].password, sizeof(s_wifi_list[i].password)
        );
    }
    s_wifi_index = 0;
}

static void apply_wifi_config(int index)
{
    if (s_wifi_count == 0) {
        return;
    }
    int i = index % s_wifi_count;
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid,     s_wifi_list[i].ssid,     sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_wifi_list[i].password, sizeof(cfg.sta.password));
    cfg.sta.scan_method      = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method      = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable  = true;
    cfg.sta.pmf_cfg.required = false;
    cfg.sta.sae_pwe_h2e      = WPA3_SAE_PWE_BOTH;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static const char *get_connected_ssid(char *buffer, size_t buffer_size)
{
    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && ap_info.ssid[0] != '\0') {
        strlcpy(buffer, (const char *)ap_info.ssid, buffer_size);
        return buffer;
    }

    wifi_config_t cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != '\0') {
        strlcpy(buffer, (const char *)cfg.sta.ssid, buffer_size);
        return buffer;
    }

    return NULL;
}

/* ── MQTT ──────────────────────────────────────────────────────────── */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;
    char event_json[256];

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(s_event_group, MQTT_CONNECTED_BIT);
        device_profile_update_mqtt(true);
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"connected\":true,\"broker\":\"%s\",\"topic\":\"%s\"}",
            APP_MQTT_URI,
            device_profile_mqtt_topic()
        );
        console_service_emit_event("mqtt", event_json);
        break;
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_event_group, MQTT_CONNECTED_BIT);
        device_profile_update_mqtt(false);
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"connected\":false,\"broker\":\"%s\",\"topic\":\"%s\"}",
            APP_MQTT_URI,
            device_profile_mqtt_topic()
        );
        console_service_emit_event("mqtt", event_json);
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    if (s_mqtt_client != NULL) {
        esp_mqtt_client_reconnect(s_mqtt_client);
        return;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = APP_MQTT_URI,
    };

    if (strlen(APP_MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = APP_MQTT_USERNAME;
    }
    if (strlen(APP_MQTT_PASSWORD) > 0) {
        mqtt_cfg.credentials.authentication.password = APP_MQTT_PASSWORD;
    }

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
}

/* ── WiFi event handler ────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
        char event_json[256];
        xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT);
        device_profile_update_wifi(false, NULL, NULL, event ? event->reason : -1);
        device_profile_update_mqtt(false);
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"connected\":false,\"ssid\":\"--\",\"ip\":\"0.0.0.0\",\"reason\":%d}",
            event ? event->reason : -1
        );
        console_service_emit_event("wifi", event_json);
        if (s_mqtt_client != NULL) {
            esp_mqtt_client_disconnect(s_mqtt_client);
        }

        if (s_reloading) {
            /* List was just updated — stay at index 0, config already applied */
            s_reloading = false;
        } else {
            /* Cycle to next entry */
            if (s_wifi_count > 1) {
                s_wifi_index = (s_wifi_index + 1) % s_wifi_count;
                apply_wifi_config(s_wifi_index);
            }
        }
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        char ssid_buffer[33] = {0};
        const char *ssid = NULL;
        char event_json[256];
        s_ip_addr = event->ip_info.ip;
        ssid = get_connected_ssid(ssid_buffer, sizeof(ssid_buffer));
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        device_profile_update_wifi(true, ssid, network_service_get_ip(), 0);
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"connected\":true,\"ssid\":\"%s\",\"ip\":\"%s\",\"reason\":0}",
            (ssid != NULL && ssid[0] != '\0') ? ssid : "--",
            network_service_get_ip()
        );
        console_service_emit_event("wifi", event_json);
        mqtt_start();
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t network_service_start(void)
{
    s_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group create failed");

    /* nvs_flash_init already called in device_profile_init; skip if already done */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    load_wifi_list();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    apply_wifi_config(s_wifi_index);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    return ESP_OK;
}

void network_service_reload_wifi_list(void)
{
    load_wifi_list();           /* refresh from device_profile */
    apply_wifi_config(0);       /* pre-load new index-0 config */
    s_reloading = true;

    if (network_service_is_wifi_ready()) {
        esp_wifi_disconnect();  /* triggers event → reconnect at index 0 */
    } else {
        s_reloading = false;
        esp_wifi_connect();
    }
}

bool network_service_is_wifi_ready(void)
{
    return (xEventGroupGetBits(s_event_group) & WIFI_CONNECTED_BIT) != 0;
}

bool network_service_is_mqtt_ready(void)
{
    return (xEventGroupGetBits(s_event_group) & MQTT_CONNECTED_BIT) != 0;
}

int network_service_get_rssi(void)
{
    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_last_rssi = ap_info.rssi;
    }
    return s_last_rssi;
}

const char *network_service_get_ip(void)
{
    static char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&s_ip_addr));
    return ip_str;
}

esp_err_t network_service_publish_json(const char *topic, const char *json_payload)
{
    ESP_RETURN_ON_FALSE(network_service_is_mqtt_ready(), ESP_ERR_INVALID_STATE, TAG, "mqtt not ready");
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, json_payload, 0, 1, 0);
    ESP_RETURN_ON_FALSE(msg_id >= 0, ESP_FAIL, TAG, "publish failed");
    return ESP_OK;
}
