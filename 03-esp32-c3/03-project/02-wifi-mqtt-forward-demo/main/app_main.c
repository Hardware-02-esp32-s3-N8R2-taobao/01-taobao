#include "esp_log.h"

#include "network_service.h"
#include "telemetry_app.h"

#define TAG "app_main"

void app_main(void)
{
    ESP_LOGI(TAG, "start wifi + mqtt forward demo");
    ESP_ERROR_CHECK(network_service_start());
    telemetry_app_run();
}
