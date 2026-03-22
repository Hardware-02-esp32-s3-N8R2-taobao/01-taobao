#include "console_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_log.h"

#include "app_config.h"
#include "device_profile.h"
#include "network_service.h"

#define TAG "console_service"

static SemaphoreHandle_t s_usb_lock;

static void usb_write_text(const char *text)
{
    bool locked = false;

    if (text == NULL) {
        return;
    }

    if (s_usb_lock != NULL) {
        locked = (xSemaphoreTake(s_usb_lock, pdMS_TO_TICKS(200)) == pdTRUE);
    }

    fputs(text, stdout);
    fflush(stdout);

    if (locked) {
        xSemaphoreGive(s_usb_lock);
    }
}

static void emit_json_line(const char *prefix, void (*builder)(char *, size_t))
{
    char *json = (char *)calloc(1, 2048);
    char *line = (char *)calloc(1, 2178);

    if (json == NULL || line == NULL) {
        usb_write_text("APP_ERROR:{\"message\":\"out of memory\"}\n");
        free(json);
        free(line);
        return;
    }

    builder(json, 2048);
    snprintf(line, 2178, "\n%s:%s\n", prefix, json);
    usb_write_text(line);

    free(json);
    free(line);
}

static void emit_wifi_list_line(void)
{
    char json[1024];
    char line[1090];

    device_profile_get_wifi_list_json(json, sizeof(json));
    snprintf(line, sizeof(line), "\nAPP_WIFI_LIST:%s\n", json);
    usb_write_text(line);
}

void console_service_emit_event(const char *event_type, const char *json_payload)
{
    char line[896];

#if !APP_SERIAL_PUSH_EVENTS
    (void)event_type;
    (void)json_payload;
    return;
#endif

    if (event_type == NULL || event_type[0] == '\0') {
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "\nAPP_EVENT:{\"type\":\"%s\",\"data\":%s}\n",
        event_type,
        (json_payload != NULL && json_payload[0] != '\0') ? json_payload : "{}"
    );
    usb_write_text(line);
}

static void console_task(void *arg)
{
    (void)arg;
    char line[1024] = {0};
    size_t line_len = 0;
    uint8_t ch = 0;

    while (1) {
        int read_len = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(50));
        if (read_len <= 0) {
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch != '\n' && line_len < sizeof(line) - 1) {
            line[line_len++] = (char)ch;
            line[line_len] = '\0';
            continue;
        }

        if (line_len == 0) {
            continue;
        }

        if (strcmp(line, "GET_STATUS") == 0) {
            emit_json_line("APP_STATUS", device_profile_build_status_json);
        } else if (strcmp(line, "GET_CONFIG") == 0) {
            emit_json_line("APP_CONFIG", device_profile_build_config_json);
        } else if (strcmp(line, "GET_OPTIONS") == 0) {
            emit_json_line("APP_OPTIONS", device_profile_build_options_json);
        } else if (strncmp(line, "SET_CONFIG ", 11) == 0) {
            char message[96];
            char response[160];
            esp_err_t ret = device_profile_apply_config_json(line + 11, message, sizeof(message));
            if (ret == ESP_OK) {
                snprintf(response, sizeof(response), "APP_OK:{\"message\":\"%s\"}\n", message);
                usb_write_text(response);
                emit_json_line("APP_CONFIG", device_profile_build_config_json);
                emit_json_line("APP_STATUS", device_profile_build_status_json);
            } else {
                snprintf(response, sizeof(response), "APP_ERROR:{\"message\":\"%s\"}\n", message);
                usb_write_text(response);
            }
        } else if (strcmp(line, "GET_WIFI_LIST") == 0) {
            char json[1024];
            char response[1088];
            device_profile_get_wifi_list_json(json, sizeof(json));
            snprintf(response, sizeof(response), "APP_WIFI_LIST:%s\n", json);
            usb_write_text(response);
        } else if (strncmp(line, "SET_WIFI_LIST ", 14) == 0) {
            char message[96];
            char response[160];
            esp_err_t ret = device_profile_set_wifi_list_json(line + 14, message, sizeof(message));
            if (ret == ESP_OK) {
                snprintf(response, sizeof(response), "APP_OK:{\"message\":\"%s\"}\n", message);
                usb_write_text(response);
                emit_wifi_list_line();
                network_service_reload_wifi_list();
                emit_json_line("APP_STATUS", device_profile_build_status_json);
            } else {
                snprintf(response, sizeof(response), "APP_ERROR:{\"message\":\"%s\"}\n", message);
                usb_write_text(response);
            }
        } else if (strcmp(line, "SCAN_WIFI") == 0) {
            char json[2048];
            char response[2112];
            network_service_get_scan_json(json, sizeof(json));
            snprintf(response, sizeof(response), "APP_EVENT:{\"type\":\"wifi_scan\",\"data\":%s}\n", json);
            usb_write_text(response);
        } else if (strcmp(line, "HELP") == 0) {
            usb_write_text("APP_OK:{\"commands\":[\"GET_STATUS\",\"GET_CONFIG\",\"GET_OPTIONS\",\"SET_CONFIG {...}\",\"GET_WIFI_LIST\",\"SET_WIFI_LIST [{...}]\",\"SCAN_WIFI\"]}\n");
        } else if (line[0] != '\0') {
            usb_write_text("APP_ERROR:{\"message\":\"unknown command\"}\n");
        }

        line_len = 0;
        line[0] = '\0';
    }
}

static void console_snapshot_task(void *arg)
{
    (void)arg;
    TickType_t last_full_dump = 0;

    vTaskDelay(pdMS_TO_TICKS(1500));

    while (1) {
        emit_json_line("APP_STATUS", device_profile_build_status_json);

        if (last_full_dump == 0 || (xTaskGetTickCount() - last_full_dump) >= pdMS_TO_TICKS(8000)) {
            emit_json_line("APP_CONFIG", device_profile_build_config_json);
            emit_json_line("APP_OPTIONS", device_profile_build_options_json);
            emit_wifi_list_line();
            last_full_dump = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t console_service_start(void)
{
    usb_serial_jtag_driver_config_t usb_cfg = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 4096,
    };
    s_usb_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_usb_lock != NULL, ESP_ERR_NO_MEM, TAG, "usb lock create failed");
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&usb_cfg), TAG, "usb serial install failed");
    BaseType_t ok = xTaskCreate(console_task, "console_task", 12288, NULL, 5, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "console task create failed");
    ok = xTaskCreate(console_snapshot_task, "console_snapshot_task", 6144, NULL, 4, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
