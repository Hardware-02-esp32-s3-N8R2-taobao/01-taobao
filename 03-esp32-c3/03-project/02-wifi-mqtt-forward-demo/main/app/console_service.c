#include "console_service.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_log.h"

#include "device_profile.h"

#define TAG "console_service"

static void usb_write_text(const char *text)
{
    if (text == NULL) {
        return;
    }
    usb_serial_jtag_write_bytes(text, strlen(text), pdMS_TO_TICKS(100));
}

static void emit_json_line(const char *prefix, void (*builder)(char *, size_t))
{
    char json[768];
    char line[832];
    builder(json, sizeof(json));
    snprintf(line, sizeof(line), "%s:%s\n", prefix, json);
    usb_write_text(line);
}

static void console_task(void *arg)
{
    (void)arg;
    char line[768] = {0};
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
        } else if (strcmp(line, "HELP") == 0) {
            usb_write_text("APP_OK:{\"commands\":[\"GET_STATUS\",\"GET_CONFIG\",\"GET_OPTIONS\",\"SET_CONFIG {...}\"]}\n");
        } else if (line[0] != '\0') {
            ESP_LOGW(TAG, "unknown command: %s", line);
            usb_write_text("APP_ERROR:{\"message\":\"unknown command\"}\n");
        }

        line_len = 0;
        line[0] = '\0';
    }
}

esp_err_t console_service_start(void)
{
    usb_serial_jtag_driver_config_t usb_cfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&usb_cfg), TAG, "usb serial install failed");
    BaseType_t ok = xTaskCreate(console_task, "console_task", 8192, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
