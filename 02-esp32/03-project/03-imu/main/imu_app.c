#include "imu_app.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "icm42688.h"

#define IMU_SPI_HOST        SPI2_HOST
#define IMU_PIN_SCLK        GPIO_NUM_18
#define IMU_PIN_MISO        GPIO_NUM_19
#define IMU_PIN_MOSI        GPIO_NUM_23
#define IMU_PIN_CS          GPIO_NUM_5
#define IMU_SPI_CLOCK_HZ    (1 * 1000 * 1000)

static const char *TAG = "imu_app";

static icm42688_t s_imu;
static bool s_imu_ready;
static bool s_streaming;
static uint32_t s_stream_count;
static uint32_t s_stream_period_ms;
static TaskHandle_t s_stream_task;
static SemaphoreHandle_t s_lock;

static void imu_stream_task(void *arg)
{
    uint32_t remaining = s_stream_count;

    while (remaining > 0) {
        if (!s_streaming) {
            break;
        }

        esp_err_t ret = imu_app_print_one_sample();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "sample read failed: %s", esp_err_to_name(ret));
            break;
        }

        remaining--;
        vTaskDelay(pdMS_TO_TICKS(s_stream_period_ms));
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_streaming = false;
    s_stream_task = NULL;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "stream finished");
    vTaskDelete(NULL);
}

esp_err_t imu_app_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "mutex create failed");

    icm42688_config_t config = {
        .host = IMU_SPI_HOST,
        .sclk_io = IMU_PIN_SCLK,
        .miso_io = IMU_PIN_MISO,
        .mosi_io = IMU_PIN_MOSI,
        .cs_io = IMU_PIN_CS,
        .clock_hz = IMU_SPI_CLOCK_HZ,
    };

    ESP_RETURN_ON_ERROR(icm42688_init(&s_imu, &config), TAG, "imu init failed");
    s_imu_ready = true;

    ESP_LOGI(TAG, "ICM42688 ready on SPI SCLK=%d MISO=%d MOSI=%d CS=%d",
             IMU_PIN_SCLK, IMU_PIN_MISO, IMU_PIN_MOSI, IMU_PIN_CS);
    return ESP_OK;
}

esp_err_t imu_app_print_who_am_i(void)
{
    uint8_t who_am_i = 0;
    ESP_RETURN_ON_ERROR(icm42688_read_who_am_i(&s_imu, &who_am_i), TAG, "who_am_i failed");
    printf("ICM42688 WHO_AM_I = 0x%02X\r\n", who_am_i);
    return ESP_OK;
}

esp_err_t imu_app_print_one_sample(void)
{
    ESP_RETURN_ON_FALSE(s_imu_ready, ESP_ERR_INVALID_STATE, TAG, "imu not ready");

    icm42688_sample_t sample = {0};
    ESP_RETURN_ON_ERROR(icm42688_read_sample(&s_imu, &sample), TAG, "read sample failed");

    printf("imu temp=%.2fC accel[g]=[%.3f, %.3f, %.3f] gyro[dps]=[%.2f, %.2f, %.2f]\r\n",
           sample.temperature_c,
           sample.accel_x_g, sample.accel_y_g, sample.accel_z_g,
           sample.gyro_x_dps, sample.gyro_y_dps, sample.gyro_z_dps);
    return ESP_OK;
}

esp_err_t imu_app_start_streaming(uint32_t sample_count, uint32_t period_ms)
{
    if (sample_count == 0) {
        sample_count = 10;
    }
    if (period_ms == 0) {
        period_ms = 200;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_streaming) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_streaming = true;
    s_stream_count = sample_count;
    s_stream_period_ms = period_ms;
    xSemaphoreGive(s_lock);

    BaseType_t ok = xTaskCreate(imu_stream_task, "imu_stream", 4096, NULL, 5, &s_stream_task);
    if (ok != pdPASS) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_streaming = false;
        s_stream_task = NULL;
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "stream start count=%lu period=%lums", (unsigned long)sample_count, (unsigned long)period_ms);
    return ESP_OK;
}

void imu_app_stop_streaming(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_streaming = false;
    xSemaphoreGive(s_lock);
}

bool imu_app_is_streaming(void)
{
    bool streaming = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    streaming = s_streaming;
    xSemaphoreGive(s_lock);
    return streaming;
}
