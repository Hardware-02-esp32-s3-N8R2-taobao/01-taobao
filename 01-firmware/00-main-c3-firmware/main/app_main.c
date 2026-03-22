#include "esp_log.h"

#include "console_service.h"
#include "device_profile.h"
#include "network_service.h"
#include "telemetry_app.h"

#define TAG "app_main"

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("wifi_init", ESP_LOG_NONE);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_NONE);
    esp_log_level_set("phy_init", ESP_LOG_NONE);
    esp_log_level_set("gpio", ESP_LOG_NONE);
    esp_log_level_set("main_task", ESP_LOG_NONE);
    esp_log_level_set("pp", ESP_LOG_NONE);
    esp_log_level_set("net80211", ESP_LOG_NONE);
    ESP_ERROR_CHECK(device_profile_init());
    ESP_ERROR_CHECK(console_service_start());
    ESP_ERROR_CHECK(network_service_start());
    telemetry_app_run();
}
