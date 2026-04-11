#include "network_service.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
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
#define WIFI_SCAN_MAX_APS  20

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
static bool                  s_scan_before_connect = false;
static TaskHandle_t          s_screen_connect_task = NULL;
static bool                  s_selected_bssid_valid = false;
static uint8_t               s_selected_bssid[6] = {0};
static uint8_t               s_selected_channel = 0;
static wifi_auth_mode_t      s_selected_authmode = WIFI_AUTH_OPEN;

static const char *wifi_disconnect_reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_UNSPECIFIED:
        return "unspecified";
    case WIFI_REASON_AUTH_EXPIRE:
        return "auth_expire";
    case WIFI_REASON_AUTH_LEAVE:
        return "auth_leave";
    case WIFI_REASON_ASSOC_EXPIRE:
        return "assoc_expire";
    case WIFI_REASON_ASSOC_TOOMANY:
        return "assoc_toomany";
    case WIFI_REASON_NOT_AUTHED:
        return "not_authed";
    case WIFI_REASON_NOT_ASSOCED:
        return "not_assoced";
    case WIFI_REASON_ASSOC_LEAVE:
        return "assoc_leave";
    case WIFI_REASON_ASSOC_NOT_AUTHED:
        return "assoc_not_authed";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4way_timeout";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake_timeout";
    case WIFI_REASON_NO_AP_FOUND:
        return "no_ap_found";
    case WIFI_REASON_AUTH_FAIL:
        return "auth_fail";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection_fail";
    default:
        return "unknown";
    }
}

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
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if (s_scan_before_connect && s_selected_bssid_valid) {
        memcpy(cfg.sta.bssid, s_selected_bssid, sizeof(s_selected_bssid));
        cfg.sta.bssid_set = true;
        cfg.sta.channel = s_selected_channel;
    }

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static bool select_visible_wifi_entry(void)
{
    if (s_wifi_count <= 0) {
        return false;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };
    uint16_t ap_count = WIFI_SCAN_MAX_APS;
    wifi_ap_record_t records[WIFI_SCAN_MAX_APS] = {0};

    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        return false;
    }
    if (esp_wifi_scan_get_ap_records(&ap_count, records) != ESP_OK) {
        return false;
    }

    int best_index = -1;
    int best_rssi = -127;
    wifi_ap_record_t best_record = {0};
    for (int wifi_index = 0; wifi_index < s_wifi_count; ++wifi_index) {
        for (uint16_t ap_index = 0; ap_index < ap_count; ++ap_index) {
            if (strcmp(s_wifi_list[wifi_index].ssid, (const char *)records[ap_index].ssid) == 0 &&
                records[ap_index].rssi > best_rssi) {
                best_index = wifi_index;
                best_rssi = records[ap_index].rssi;
                best_record = records[ap_index];
            }
        }
    }

    if (best_index >= 0) {
        s_wifi_index = best_index;
        memcpy(s_selected_bssid, best_record.bssid, sizeof(s_selected_bssid));
        s_selected_bssid_valid = true;
        s_selected_channel = best_record.primary;
        s_selected_authmode = best_record.authmode;
        apply_wifi_config(s_wifi_index);
        ESP_LOGI(
            TAG,
            "selected visible ap ssid=%s channel=%u rssi=%d auth=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X",
            s_wifi_list[s_wifi_index].ssid,
            s_selected_channel,
            best_rssi,
            best_record.authmode,
            s_selected_bssid[0], s_selected_bssid[1], s_selected_bssid[2],
            s_selected_bssid[3], s_selected_bssid[4], s_selected_bssid[5]
        );
        return true;
    }

    s_selected_bssid_valid = false;
    s_selected_channel = 0;
    s_selected_authmode = WIFI_AUTH_OPEN;
    return false;
}

static void connect_using_current_strategy(void)
{
    if (s_scan_before_connect) {
        if (s_screen_connect_task != NULL) {
            xTaskNotifyGive(s_screen_connect_task);
        }
        return;
    }
    esp_wifi_connect();
}

static void screen_connect_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(600));

        if (!select_visible_wifi_entry()) {
            ESP_LOGW(TAG, "no configured ssid visible during scan");
        }
        esp_wifi_connect();
    }
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

static void mqtt_stop(void)
{
    if (s_mqtt_client == NULL) {
        return;
    }
    esp_mqtt_client_stop(s_mqtt_client);
    esp_mqtt_client_destroy(s_mqtt_client);
    s_mqtt_client = NULL;
}

/* ── WiFi event handler ────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        connect_using_current_strategy();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
        char event_json[256];
        const uint8_t reason = event ? event->reason : 0;
        xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT);
        device_profile_update_wifi(false, NULL, NULL, event ? event->reason : -1);
        device_profile_update_mqtt(false);
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"connected\":false,\"ssid\":\"--\",\"ip\":\"0.0.0.0\",\"reason\":%d,\"reasonText\":\"%s\"}",
            event ? event->reason : -1,
            wifi_disconnect_reason_text(reason)
        );
        console_service_emit_event("wifi", event_json);
        ESP_LOGW(TAG, "wifi disconnected, reason=%u (%s)", reason, wifi_disconnect_reason_text(reason));
        if (s_mqtt_client != NULL) {
            esp_mqtt_client_disconnect(s_mqtt_client);
        }

        if (s_reloading) {
            /* List was just updated — stay at index 0, config already applied */
            s_reloading = false;
        }
        connect_using_current_strategy();

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
    s_scan_before_connect = true;
    if (s_scan_before_connect && s_screen_connect_task == NULL) {
        xTaskCreate(screen_connect_task, "screen_wifi_connect", 4096, NULL, 5, &s_screen_connect_task);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    apply_wifi_config(s_wifi_index);
    ESP_ERROR_CHECK(esp_wifi_start());
    if (device_profile_hardware_variant() == DEVICE_HW_VARIANT_OLED_SCREEN) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(34));
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
        ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    }
    network_service_set_power_save(device_profile_low_power_enabled());

    return ESP_OK;
}

void network_service_reload_wifi_list(void)
{
    load_wifi_list();
    s_wifi_index = 0;
    s_reloading = true;
    s_selected_bssid_valid = false;
    s_selected_channel = 0;
    s_selected_authmode = WIFI_AUTH_OPEN;
    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT);
    device_profile_update_wifi(false, NULL, NULL, 0);
    device_profile_update_mqtt(false);
    if (s_mqtt_client != NULL) {
        esp_mqtt_client_disconnect(s_mqtt_client);
    }

    esp_wifi_disconnect();
    esp_wifi_stop();
    apply_wifi_config(s_wifi_index);
    esp_wifi_start();
}

void network_service_set_power_save(bool enabled)
{
    wifi_ps_type_t ps_type = enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
    esp_err_t ret = esp_wifi_set_ps(ps_type);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "set wifi ps failed: %s", esp_err_to_name(ret));
    }
}

static const char *auth_mode_text(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "wep";
    case WIFI_AUTH_WPA_PSK:
        return "wpa";
    case WIFI_AUTH_WPA2_PSK:
        return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "wpa/wpa2";
    case WIFI_AUTH_WPA3_PSK:
        return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "wpa2/wpa3";
    default:
        return "unknown";
    }
}

void network_service_get_scan_json(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(250));

    esp_err_t scan_ret = esp_wifi_scan_start(&scan_cfg, true);
    apply_wifi_config(s_wifi_index);
    esp_wifi_connect();

    if (scan_ret != ESP_OK) {
        snprintf(buffer, buffer_size, "{\"ok\":false,\"message\":\"scan start failed\",\"code\":%d}", (int)scan_ret);
        return;
    }

    uint16_t ap_count = WIFI_SCAN_MAX_APS;
    wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        snprintf(buffer, buffer_size, "{\"ok\":false,\"message\":\"out of memory\"}");
        return;
    }

    if (esp_wifi_scan_get_ap_records(&ap_count, records) != ESP_OK) {
        free(records);
        snprintf(buffer, buffer_size, "{\"ok\":false,\"message\":\"scan read failed\"}");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "accessPoints");
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "count", ap_count);

    for (uint16_t i = 0; i < ap_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (const char *)records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(item, "channel", records[i].primary);
        cJSON_AddStringToObject(item, "auth", auth_mode_text(records[i].authmode));
        cJSON_AddItemToArray(list, item);
    }

    char *printed = cJSON_PrintUnformatted(root);
    snprintf(buffer, buffer_size, "%s", printed != NULL ? printed : "{\"ok\":false}");
    cJSON_free(printed);
    cJSON_Delete(root);
    free(records);
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

void network_service_prepare_for_sleep(void)
{
    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT);
    device_profile_update_wifi(false, NULL, NULL, 0);
    device_profile_update_mqtt(false);

    if (s_mqtt_client != NULL) {
        mqtt_stop();
    }

    esp_wifi_disconnect();
    esp_wifi_stop();
}
