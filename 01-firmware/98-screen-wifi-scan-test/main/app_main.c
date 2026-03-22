#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define TEST_WIFI_SSID "Ermao"
#define TEST_WIFI_PASSWORD "gf666666"

static const char *TAG = "screen_wifi_test";
static bool s_target_found = false;
static uint8_t s_target_bssid[6] = {0};
static uint8_t s_target_channel = 0;
static bool s_allow_autoconnect = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_allow_autoconnect) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        printf("WIFI_DISCONNECTED: reason=%d\n", event ? event->reason : -1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (event != NULL) {
            printf("WIFI_GOT_IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        }
    }
}

static void prepare_target_ap(void)
{
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    uint16_t ap_count = 16;
    wifi_ap_record_t records[16] = {0};
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, records));

    s_target_found = false;
    for (uint16_t i = 0; i < ap_count; ++i) {
        if (strcmp((const char *)records[i].ssid, TEST_WIFI_SSID) == 0) {
            memcpy(s_target_bssid, records[i].bssid, sizeof(s_target_bssid));
            s_target_channel = records[i].primary;
            s_target_found = true;
            ESP_LOGI(TAG,
                     "found target ssid=%s rssi=%d channel=%u auth=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X",
                     TEST_WIFI_SSID,
                     records[i].rssi,
                     records[i].primary,
                     records[i].authmode,
                     records[i].bssid[0], records[i].bssid[1], records[i].bssid[2],
                     records[i].bssid[3], records[i].bssid[4], records[i].bssid[5]);
            break;
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    uint8_t custom_mac[6] = {0x12, 0x00, 0x3B, 0xC6, 0x04, 0xC8};
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, custom_mac));
    printf("TEST_MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           custom_mac[0], custom_mac[1], custom_mac[2],
           custom_mac[3], custom_mac[4], custom_mac[5]);

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, TEST_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, TEST_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = false;
    wifi_cfg.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(34));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));

    prepare_target_ap();
    if (s_target_found) {
        memcpy(wifi_cfg.sta.bssid, s_target_bssid, sizeof(s_target_bssid));
        wifi_cfg.sta.bssid_set = true;
        wifi_cfg.sta.channel = s_target_channel;
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    s_allow_autoconnect = true;
    ESP_ERROR_CHECK(esp_wifi_connect());

    printf("BOARD_WIFI_CONNECT_TEST_READY\n");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
