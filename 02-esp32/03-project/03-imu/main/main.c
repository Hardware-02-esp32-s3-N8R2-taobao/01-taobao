#include "esp_log.h"

#include "imu_app.h"
#include "imu_shell.h"

static const char *TAG = "imu_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting 03-imu demo");
    esp_err_t ret = imu_app_init();
    if (ret == ESP_OK) {
        imu_app_print_who_am_i();
    } else {
        ESP_LOGW(TAG, "IMU init failed, shell still available: %s", esp_err_to_name(ret));
    }
    imu_shell_start();
}
