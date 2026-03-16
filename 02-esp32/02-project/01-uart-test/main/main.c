#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "basic_demo";

void app_main(void)
{
    uint32_t heartbeat = 0;
    const char *message = "hello from esp32 on COM84";

    ESP_LOGI(TAG, "ESP32 basic serial demo starting");
    ESP_LOGI(TAG, "Chip target: ESP32");
    ESP_LOGI(TAG, "Serial port for this board: COM84");

    while (1) {
        ESP_LOGI(TAG, "Heartbeat %lu", (unsigned long)heartbeat);
        printf("%s heartbeat=%lu\r\n", message, (unsigned long)heartbeat);

        heartbeat++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
