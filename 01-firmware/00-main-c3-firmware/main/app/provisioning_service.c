#include "provisioning_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "app_config.h"
#include "device_profile.h"
#include "network_service.h"
#include "status_led.h"
#include "telemetry_app.h"

#define TAG "prov_service"

static bool s_initialized = false;
static bool s_active = false;
static bool s_transitioning = false;
static bool s_has_new_credentials = false;
static char s_received_ssid[33] = {0};
static char s_received_password[65] = {0};
static char s_service_name[24] = {0};

static void strengthen_softap_config(void)
{
    wifi_config_t ap_cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) != ESP_OK) {
        return;
    }

    ap_cfg.ap.channel = APP_WIFI_PROVISIONING_AP_CHANNEL;
    ap_cfg.ap.ssid_hidden = 0;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.beacon_interval = 100;
    if (strlen(APP_WIFI_PROVISIONING_AP_PASSWORD) >= 8) {
        ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_max_tx_power(60));
}

static void build_service_name(char *buffer, size_t buffer_size)
{
    uint8_t mac[6] = {0};

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(
        buffer,
        buffer_size,
        "%s%02X%02X%02X",
        APP_WIFI_PROVISIONING_AP_PREFIX,
        mac[3],
        mac[4],
        mac[5]
    );
}

static void provisioning_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base != WIFI_PROV_EVENT) {
        return;
    }

    switch (event_id) {
    case WIFI_PROV_START:
        s_transitioning = false;
        s_active = true;
        status_led_set_provisioning(true);
        ESP_LOGI(TAG, "softap provisioning started: ssid=%s", s_service_name);
        break;
    case WIFI_PROV_CRED_RECV: {
        const wifi_sta_config_t *wifi_sta_cfg = (const wifi_sta_config_t *)event_data;
        if (wifi_sta_cfg != NULL) {
            snprintf(s_received_ssid, sizeof(s_received_ssid), "%s", (const char *)wifi_sta_cfg->ssid);
            snprintf(s_received_password, sizeof(s_received_password), "%s", (const char *)wifi_sta_cfg->password);
            ESP_LOGI(TAG, "received provisioning credentials for ssid=%s", s_received_ssid);
        }
        break;
    }
    case WIFI_PROV_CRED_SUCCESS: {
        char message[96] = {0};
        if (device_profile_replace_wifi_credential(s_received_ssid, s_received_password, message, sizeof(message)) == ESP_OK) {
            s_has_new_credentials = true;
            ESP_LOGI(TAG, "saved provisioned wifi credentials: %s", message);
        } else {
            ESP_LOGW(TAG, "failed to save provisioned wifi credentials");
        }
        break;
    }
    case WIFI_PROV_CRED_FAIL:
        ESP_LOGW(TAG, "provisioning credentials rejected by station");
        break;
    case WIFI_PROV_END:
        s_transitioning = false;
        s_active = false;
        status_led_set_provisioning(false);
        ESP_LOGI(TAG, "softap provisioning ended");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
        network_service_set_power_save(device_profile_should_enable_wifi_power_save());
        if (s_has_new_credentials) {
            s_has_new_credentials = false;
            network_service_reload_wifi_list();
            telemetry_app_request_immediate_cycle();
        }
        break;
    default:
        break;
    }
}

esp_err_t provisioning_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };

    ESP_RETURN_ON_ERROR(wifi_prov_mgr_init(config), TAG, "wifi_prov_mgr_init failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &provisioning_event_handler, NULL),
        TAG,
        "register provisioning event handler failed"
    );
    s_initialized = true;
    return ESP_OK;
}

esp_err_t provisioning_service_start(void)
{
    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(provisioning_service_init(), TAG, "provisioning init failed");
    }
    if (s_active || s_transitioning) {
        return ESP_OK;
    }

    s_transitioning = true;
    memset(s_received_ssid, 0, sizeof(s_received_ssid));
    memset(s_received_password, 0, sizeof(s_received_password));
    s_has_new_credentials = false;
    build_service_name(s_service_name, sizeof(s_service_name));
    ESP_LOGI(
        TAG,
        "starting provisioning ap ssid=%s password=%s",
        s_service_name,
        APP_WIFI_PROVISIONING_AP_PASSWORD
    );
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        ESP_LOGI(TAG, "wifi mode before provisioning start=%d", (int)mode);
    }
    ESP_LOGI(TAG, "calling wifi_prov_mgr_start_provisioning");

    esp_err_t ret = wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_0,
        NULL,
        s_service_name,
        APP_WIFI_PROVISIONING_AP_PASSWORD
    );
    ESP_LOGI(TAG, "wifi_prov_mgr_start_provisioning returned: %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_start_provisioning failed: %s", esp_err_to_name(ret));
        s_transitioning = false;
        s_active = false;
    } else {
        strengthen_softap_config();
    }
    return ret;
}

bool provisioning_service_is_active(void)
{
    return s_active || s_transitioning;
}

bool provisioning_service_is_transitioning(void)
{
    return s_transitioning;
}
