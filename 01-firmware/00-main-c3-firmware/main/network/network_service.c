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
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "console_service.h"
#include "device_profile.h"
#include "provisioning_service.h"
#include "remote_config_service.h"

#define TAG "network_service"
#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1
#define MQTT_PUBLISHED_BIT BIT2
#define WIFI_MAX_ENTRIES   8
#define WIFI_SCAN_MAX_APS  20
#define WIFI_COLD_BOOT_CONNECT_DELAY_MS 5000

typedef struct {
    char ssid[33];
    char password[65];
} ns_wifi_entry_t;

static EventGroupHandle_t    s_event_group;
static esp_mqtt_client_handle_t s_mqtt_client;
static esp_netif_t          *s_wifi_sta_netif = NULL;
static esp_netif_t          *s_wifi_ap_netif = NULL;
static esp_ip4_addr_t        s_ip_addr;
static int                   s_last_rssi = -127;

static ns_wifi_entry_t       s_wifi_list[WIFI_MAX_ENTRIES];
static int                   s_wifi_count = 0;
static int                   s_wifi_index = 0;
static bool                  s_reloading  = false;
static bool                  s_scan_before_connect = false;
static TaskHandle_t          s_screen_connect_task = NULL;
static TaskHandle_t          s_network_monitor_task = NULL;
static bool                  s_selected_bssid_valid = false;
static uint8_t               s_selected_bssid[6] = {0};
static uint8_t               s_selected_channel = 0;
static wifi_auth_mode_t      s_selected_authmode = WIFI_AUTH_OPEN;
static int                   s_last_publish_msg_id = -1;
static int64_t               s_connect_attempt_started_ms = 0;
static int64_t               s_last_connect_kick_ms = 0;
static bool                  s_sta_compat_mode = false;
static int64_t               s_connect_ready_after_ms = 0;
static bool                  s_connect_delay_logged = false;
static char                  s_mqtt_rx_topic[128] = {0};
static char                  s_mqtt_rx_payload[1024] = {0};
static int                   s_mqtt_rx_expected_len = 0;
static int                   s_mqtt_rx_received_len = 0;
static bool                  s_mqtt_rx_active = false;

static void screen_connect_task(void *arg);

static void reset_mqtt_rx_buffer(void)
{
    memset(s_mqtt_rx_topic, 0, sizeof(s_mqtt_rx_topic));
    memset(s_mqtt_rx_payload, 0, sizeof(s_mqtt_rx_payload));
    s_mqtt_rx_expected_len = 0;
    s_mqtt_rx_received_len = 0;
    s_mqtt_rx_active = false;
}

static void handle_mqtt_data_event(const esp_mqtt_event_handle_t event)
{
    if (
        event == NULL ||
        event->topic == NULL ||
        event->data == NULL ||
        event->topic_len <= 0 ||
        event->topic_len >= (int)sizeof(s_mqtt_rx_topic) ||
        event->total_data_len <= 0 ||
        event->total_data_len >= (int)sizeof(s_mqtt_rx_payload)
    ) {
        reset_mqtt_rx_buffer();
        return;
    }

    if (event->current_data_offset == 0) {
        reset_mqtt_rx_buffer();
        memcpy(s_mqtt_rx_topic, event->topic, (size_t)event->topic_len);
        s_mqtt_rx_topic[event->topic_len] = '\0';
        s_mqtt_rx_expected_len = event->total_data_len;
        s_mqtt_rx_active = true;
    }

    if (
        !s_mqtt_rx_active ||
        s_mqtt_rx_expected_len != event->total_data_len ||
        s_mqtt_rx_received_len != event->current_data_offset
    ) {
        reset_mqtt_rx_buffer();
        return;
    }

    if ((s_mqtt_rx_received_len + event->data_len) >= (int)sizeof(s_mqtt_rx_payload)) {
        reset_mqtt_rx_buffer();
        return;
    }

    memcpy(
        s_mqtt_rx_payload + s_mqtt_rx_received_len,
        event->data,
        (size_t)event->data_len
    );
    s_mqtt_rx_received_len += event->data_len;

    if (s_mqtt_rx_received_len >= s_mqtt_rx_expected_len) {
        s_mqtt_rx_payload[s_mqtt_rx_expected_len] = '\0';
        remote_config_service_handle_mqtt_message(
            s_mqtt_rx_topic,
            s_mqtt_rx_payload,
            s_mqtt_rx_expected_len
        );
        reset_mqtt_rx_buffer();
    }
}

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

static bool wifi_disconnect_reason_needs_sta_compat(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_AUTH_LEAVE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
        return true;
    default:
        return false;
    }
}

static bool wifi_disconnect_reason_should_enable_scan(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_AUTH_LEAVE:
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_ASSOC_TOOMANY:
    case WIFI_REASON_NOT_AUTHED:
    case WIFI_REASON_NOT_ASSOCED:
    case WIFI_REASON_ASSOC_NOT_AUTHED:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
        return true;
    default:
        return false;
    }
}

static bool default_scan_before_connect(void)
{
    return device_profile_hardware_variant() == DEVICE_HW_VARIANT_OLED_SCREEN;
}

static void ensure_scan_connect_task(void)
{
    if (s_screen_connect_task == NULL) {
        xTaskCreate(screen_connect_task, "screen_wifi_connect", 4096, NULL, 5, &s_screen_connect_task);
    }
}

static void restore_default_wifi_strategy(void)
{
    s_scan_before_connect = default_scan_before_connect();
    s_selected_bssid_valid = false;
    memset(s_selected_bssid, 0, sizeof(s_selected_bssid));
    s_selected_channel = 0;
    s_selected_authmode = WIFI_AUTH_OPEN;
    s_sta_compat_mode = false;
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
    cfg.sta.failure_retry_cnt = 2;
    cfg.sta.bssid_set = false;
    cfg.sta.channel = (s_scan_before_connect && s_selected_channel > 0) ? s_selected_channel : 0;
    if (s_sta_compat_mode) {
        cfg.sta.pmf_cfg.capable = false;
        cfg.sta.pmf_cfg.required = false;
        cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;
    } else {
        cfg.sta.pmf_cfg.capable = true;
        cfg.sta.pmf_cfg.required = false;
        cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
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
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (s_connect_ready_after_ms > 0 && now_ms < s_connect_ready_after_ms) {
        if (!s_connect_delay_logged) {
            ESP_LOGI(
                TAG,
                "delaying initial WiFi connect for %lld ms after cold boot",
                (long long)(s_connect_ready_after_ms - now_ms)
            );
            s_connect_delay_logged = true;
        }
        return;
    }

    s_connect_ready_after_ms = 0;
    s_last_connect_kick_ms = now_ms;
    if (s_scan_before_connect) {
        if (s_screen_connect_task != NULL) {
            xTaskNotifyGive(s_screen_connect_task);
        }
        return;
    }
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
    }
}

static void network_monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        bool wifi_ready = network_service_is_wifi_ready();

        if (wifi_ready) {
            s_connect_attempt_started_ms = 0;
        } else if (!provisioning_service_is_active()) {
            if (s_connect_attempt_started_ms == 0) {
                s_connect_attempt_started_ms = now_ms;
                connect_using_current_strategy();
            } else if ((now_ms - s_connect_attempt_started_ms) >= APP_WIFI_PROVISIONING_TIMEOUT_MS) {
                ESP_LOGW(TAG, "wifi unavailable for %d ms, entering softap provisioning", APP_WIFI_PROVISIONING_TIMEOUT_MS);
                if (provisioning_service_start() == ESP_OK) {
                    s_connect_attempt_started_ms = 0;
                }
            } else if ((now_ms - s_last_connect_kick_ms) >= 5000) {
                connect_using_current_strategy();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void screen_connect_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (provisioning_service_is_active()) {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(600));
        if (provisioning_service_is_active()) {
            continue;
        }

        if (!select_visible_wifi_entry()) {
            ESP_LOGW(TAG, "no configured ssid visible during scan");
        }
        if (provisioning_service_is_active()) {
            continue;
        }
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect after scan failed: %s", esp_err_to_name(ret));
        }
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
    char event_json[256];

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(s_event_group, MQTT_CONNECTED_BIT);
        device_profile_update_mqtt(true);
        reset_mqtt_rx_buffer();
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"connected\":true,\"broker\":\"%s\",\"topic\":\"%s\"}",
            APP_MQTT_URI,
            device_profile_mqtt_topic()
        );
        console_service_emit_event("mqtt", event_json);
        if (s_mqtt_client != NULL) {
            char ack_topic[64];
            snprintf(ack_topic, sizeof(ack_topic), "%s/+/ack", APP_MQTT_TOPIC_PREFIX);
            if (esp_mqtt_client_subscribe(s_mqtt_client, ack_topic, 1) < 0) {
                ESP_LOGW(TAG, "subscribe ack topic failed: %s", ack_topic);
            }
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_event_group, MQTT_CONNECTED_BIT | MQTT_PUBLISHED_BIT);
        device_profile_update_mqtt(false);
        reset_mqtt_rx_buffer();
        snprintf(
            event_json,
            sizeof(event_json),
            "{\"connected\":false,\"broker\":\"%s\",\"topic\":\"%s\"}",
            APP_MQTT_URI,
            device_profile_mqtt_topic()
        );
        console_service_emit_event("mqtt", event_json);
        break;
    case MQTT_EVENT_PUBLISHED: {
        const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
        if (event != NULL && event->msg_id == s_last_publish_msg_id) {
            xEventGroupSetBits(s_event_group, MQTT_PUBLISHED_BIT);
        }
        break;
    }
    case MQTT_EVENT_DATA:
        handle_mqtt_data_event((const esp_mqtt_event_handle_t)event_data);
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
        if (!provisioning_service_is_active()) {
            connect_using_current_strategy();
        }

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
        char event_json[256];
        const uint8_t reason = event ? event->reason : 0;
        bool strategy_changed = false;
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

        if (!s_scan_before_connect && wifi_disconnect_reason_should_enable_scan(reason)) {
            s_scan_before_connect = true;
            s_selected_channel = 0;
            s_selected_authmode = WIFI_AUTH_OPEN;
            ensure_scan_connect_task();
            strategy_changed = true;
            ESP_LOGW(TAG, "switching reconnect strategy to scan-before-connect");
        }

        if (wifi_disconnect_reason_needs_sta_compat(reason) && !s_sta_compat_mode) {
            s_sta_compat_mode = true;
            s_selected_bssid_valid = false;
            s_selected_channel = 0;
            memset(s_selected_bssid, 0, sizeof(s_selected_bssid));
            s_selected_authmode = WIFI_AUTH_OPEN;
            strategy_changed = true;
            ESP_LOGW(TAG, "switching STA reconnect strategy to compatibility mode");
        }

        if (strategy_changed) {
            apply_wifi_config(s_wifi_index);
        }

        if (s_reloading) {
            /* List was just updated — stay at index 0, config already applied */
            s_reloading = false;
        }
        if (!provisioning_service_is_active()) {
            if (s_connect_attempt_started_ms == 0) {
                s_connect_attempt_started_ms = esp_timer_get_time() / 1000;
            }
            connect_using_current_strategy();
        }

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
        restore_default_wifi_strategy();
        s_connect_attempt_started_ms = 0;
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t network_service_start(void)
{
    s_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group create failed");
    xEventGroupClearBits(s_event_group, MQTT_PUBLISHED_BIT);

    /* nvs_flash_init already called in device_profile_init; skip if already done */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_wifi_sta_netif != NULL, ESP_FAIL, TAG, "create default wifi sta netif failed");
    s_wifi_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_wifi_ap_netif != NULL, ESP_FAIL, TAG, "create default wifi ap netif failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    load_wifi_list();
    restore_default_wifi_strategy();
    if (s_scan_before_connect) {
        ensure_scan_connect_task();
    }
    s_connect_ready_after_ms = 0;
    s_connect_delay_logged = false;
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    if (
        device_profile_hardware_variant() == DEVICE_HW_VARIANT_SUPERMINI &&
        (reset_reason == ESP_RST_POWERON || reset_reason == ESP_RST_BROWNOUT)
    ) {
        s_connect_ready_after_ms = (esp_timer_get_time() / 1000) + WIFI_COLD_BOOT_CONNECT_DELAY_MS;
        ESP_LOGI(
            TAG,
            "cold boot detected (reset reason=%d), postpone first WiFi connect by %d ms",
            (int)reset_reason,
            WIFI_COLD_BOOT_CONNECT_DELAY_MS
        );
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    apply_wifi_config(s_wifi_index);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    if (device_profile_hardware_variant() == DEVICE_HW_VARIANT_OLED_SCREEN) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(34));
    }
    network_service_set_power_save(device_profile_should_enable_wifi_power_save());
    s_connect_attempt_started_ms = esp_timer_get_time() / 1000;
    s_last_connect_kick_ms = 0;
    if (s_network_monitor_task == NULL) {
        xTaskCreate(network_monitor_task, "network_monitor", 3072, NULL, 4, &s_network_monitor_task);
    }

    return ESP_OK;
}

void network_service_reload_wifi_list(void)
{
    load_wifi_list();
    s_wifi_index = 0;
    s_reloading = true;
    restore_default_wifi_strategy();
    if (s_scan_before_connect) {
        ensure_scan_connect_task();
    }
    s_connect_attempt_started_ms = esp_timer_get_time() / 1000;
    s_last_connect_kick_ms = 0;
    s_connect_ready_after_ms = 0;
    s_connect_delay_logged = false;
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

    if (provisioning_service_is_active()) {
        snprintf(buffer, buffer_size, "{\"ok\":false,\"message\":\"provisioning active\"}");
        return;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };
    const bool was_connected = network_service_is_wifi_ready();
    wifi_ps_type_t previous_ps = WIFI_PS_NONE;
    bool restore_ps = false;

    if (esp_wifi_get_ps(&previous_ps) == ESP_OK) {
        restore_ps = true;
        if (previous_ps != WIFI_PS_NONE) {
            esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
            if (ps_ret != ESP_OK) {
                ESP_LOGW(TAG, "disable wifi ps before scan failed: %s", esp_err_to_name(ps_ret));
            }
        }
    }

    esp_err_t scan_ret = esp_wifi_scan_start(&scan_cfg, true);

    if (restore_ps && previous_ps != WIFI_PS_NONE) {
        esp_err_t ps_ret = esp_wifi_set_ps(previous_ps);
        if (ps_ret != ESP_OK) {
            ESP_LOGW(TAG, "restore wifi ps after scan failed: %s", esp_err_to_name(ps_ret));
        }
    }

    if (!was_connected && !provisioning_service_is_active()) {
        connect_using_current_strategy();
    }

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
    s_last_publish_msg_id = msg_id;
    if (s_event_group != NULL) {
        xEventGroupClearBits(s_event_group, MQTT_PUBLISHED_BIT);
    }
    return ESP_OK;
}

bool network_service_wait_for_publish(int timeout_ms)
{
    if (s_event_group == NULL || s_last_publish_msg_id < 0) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        MQTT_PUBLISHED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 0)
    );
    return (bits & MQTT_PUBLISHED_BIT) != 0;
}

void network_service_prepare_for_sleep(void)
{
    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT | MQTT_PUBLISHED_BIT);
    device_profile_update_wifi(false, NULL, NULL, 0);
    device_profile_update_mqtt(false);
    s_last_publish_msg_id = -1;
    reset_mqtt_rx_buffer();

    if (s_mqtt_client != NULL) {
        mqtt_stop();
    }

    esp_wifi_disconnect();
    esp_wifi_stop();
}

bool network_service_is_provisioning_active(void)
{
    return provisioning_service_is_active();
}
