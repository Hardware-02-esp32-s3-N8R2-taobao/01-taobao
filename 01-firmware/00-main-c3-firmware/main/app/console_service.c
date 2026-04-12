#include "console_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"

#include "app_config.h"
#include "bmp180_sensor.h"
#include "device_profile.h"
#include "network_service.h"
#include "ota_service.h"
#include "sensor_bus.h"
#include "telemetry_app.h"

#define TAG "console_service"
#define CONSOLE_LINE_BUFFER_SIZE 4096
#define CONSOLE_READ_BUFFER_SIZE 512
#define OTA_RAW_CHUNK_MAX 2048

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

static esp_err_t probe_i2c_addr(i2c_port_t port, uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(30));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void emit_i2c_scan_line(void)
{
    char json[768];
    char line[896];
    size_t used = 0;
    bool first = true;

    esp_err_t init_ret = sensor_bus_init();
    if (init_ret != ESP_OK) {
        snprintf(line, sizeof(line), "APP_ERROR:{\"message\":\"i2c init failed: %s\"}\n", esp_err_to_name(init_ret));
        usb_write_text(line);
        return;
    }

    used += (size_t)snprintf(
        json + used,
        sizeof(json) - used,
        "{\"port\":%d,\"sda\":%d,\"scl\":%d,\"found\":[",
        (int)sensor_bus_i2c_port(),
        (int)sensor_bus_i2c_sda_gpio(),
        (int)sensor_bus_i2c_scl_gpio()
    );

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (probe_i2c_addr(sensor_bus_i2c_port(), addr) != ESP_OK) {
            continue;
        }
        used += (size_t)snprintf(
            json + used,
            sizeof(json) - used,
            "%s%d",
            first ? "" : ",",
            addr
        );
        first = false;
        if (used >= sizeof(json) - 8) {
            break;
        }
    }

    snprintf(json + used, sizeof(json) - used, "]}");
    snprintf(line, sizeof(line), "APP_EVENT:{\"type\":\"i2c_scan\",\"data\":%s}\n", json);
    usb_write_text(line);
}

static void emit_ota_status_line(void)
{
    char json[320];
    char line[384];

    ota_service_build_serial_status_json(json, sizeof(json));
    snprintf(line, sizeof(line), "APP_OTA:%s\n", json);
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
    char line[CONSOLE_LINE_BUFFER_SIZE] = {0};
    size_t line_len = 0;
    uint8_t buffer[CONSOLE_READ_BUFFER_SIZE];
    uint8_t ota_raw_chunk[OTA_RAW_CHUNK_MAX];
    size_t ota_raw_expected_size = 0;
    size_t ota_raw_received_size = 0;

    while (1) {
        int read_len = usb_serial_jtag_read_bytes(buffer, sizeof(buffer), pdMS_TO_TICKS(20));
        if (read_len <= 0) {
            continue;
        }

        for (int i = 0; i < read_len; ++i) {
            uint8_t ch = buffer[i];

            if (ota_raw_expected_size > 0) {
                ota_raw_chunk[ota_raw_received_size++] = ch;
                if (ota_raw_received_size >= ota_raw_expected_size) {
                    char message[128];
                    char response[192];
                    size_t written_size = 0;
                    esp_err_t ret = ota_service_serial_write_binary(
                        ota_raw_chunk,
                        ota_raw_expected_size,
                        &written_size,
                        message,
                        sizeof(message)
                    );
                    if (ret == ESP_OK) {
                        emit_ota_status_line();
                    } else {
                        snprintf(response, sizeof(response), "APP_ERROR:{\"message\":\"%s\"}\n", message);
                        usb_write_text(response);
                        emit_ota_status_line();
                    }
                    ota_raw_expected_size = 0;
                    ota_raw_received_size = 0;
                }
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

            if (strcmp(line, "OTA_STATUS") == 0) {
                emit_ota_status_line();
            } else if (strncmp(line, "OTA_BEGIN ", 10) == 0) {
                char message[128];
                cJSON *root = cJSON_Parse(line + 10);
                if (root == NULL) {
                    usb_write_text("APP_ERROR:{\"message\":\"invalid ota metadata\"}\n");
                } else {
                    cJSON *size_item = cJSON_GetObjectItemCaseSensitive(root, "size");
                    cJSON *sha_item = cJSON_GetObjectItemCaseSensitive(root, "sha256");
                    cJSON *version_item = cJSON_GetObjectItemCaseSensitive(root, "version");
                    if (!cJSON_IsNumber(size_item)) {
                        usb_write_text("APP_ERROR:{\"message\":\"ota size missing\"}\n");
                    } else {
                        esp_err_t ret = ota_service_serial_begin(
                            (size_t)cJSON_GetNumberValue(size_item),
                            cJSON_IsString(sha_item) ? sha_item->valuestring : "",
                            cJSON_IsString(version_item) ? version_item->valuestring : "",
                            message,
                            sizeof(message)
                        );
                        if (ret == ESP_OK) {
                            emit_ota_status_line();
                        } else {
                            char response[192];
                            snprintf(response, sizeof(response), "APP_ERROR:{\"message\":\"%s\"}\n", message);
                            usb_write_text(response);
                            emit_ota_status_line();
                        }
                    }
                    cJSON_Delete(root);
                }
            } else if (strncmp(line, "OTA_WRITE_RAW ", 14) == 0) {
                char *endptr = NULL;
                unsigned long requested_size = strtoul(line + 14, &endptr, 10);
                if (endptr == (line + 14) || (endptr != NULL && *endptr != '\0')) {
                    usb_write_text("APP_ERROR:{\"message\":\"ota raw size invalid\"}\n");
                } else if (requested_size == 0 || requested_size > OTA_RAW_CHUNK_MAX) {
                    usb_write_text("APP_ERROR:{\"message\":\"ota raw size out of range\"}\n");
                } else {
                    ota_raw_expected_size = (size_t)requested_size;
                    ota_raw_received_size = 0;
                }
            } else if (strncmp(line, "OTA_WRITE ", 10) == 0) {
                char message[128];
                size_t written_size = 0;
                esp_err_t ret = ota_service_serial_write_hex(line + 10, &written_size, message, sizeof(message));
                if (ret == ESP_OK) {
                    emit_ota_status_line();
                } else {
                    char response[192];
                    snprintf(response, sizeof(response), "APP_ERROR:{\"message\":\"%s\"}\n", message);
                    usb_write_text(response);
                    emit_ota_status_line();
                }
            } else if (strcmp(line, "OTA_FINISH") == 0) {
                char message[128];
                esp_err_t ret = ota_service_serial_finish(message, sizeof(message));
                if (ret == ESP_OK) {
                    emit_ota_status_line();
                    usb_write_text("APP_OK:{\"message\":\"serial ota complete, rebooting\"}\n");
                    vTaskDelay(pdMS_TO_TICKS(800));
                    esp_restart();
                } else {
                    char response[192];
                    snprintf(response, sizeof(response), "APP_ERROR:{\"message\":\"%s\"}\n", message);
                    usb_write_text(response);
                    emit_ota_status_line();
                }
            } else if (strcmp(line, "OTA_ABORT") == 0) {
                char message[128];
                esp_err_t ret = ota_service_serial_abort("serial ota aborted", message, sizeof(message));
                if (ret == ESP_OK) {
                    usb_write_text("APP_OK:{\"message\":\"serial ota aborted\"}\n");
                    emit_ota_status_line();
                } else {
                    usb_write_text("APP_ERROR:{\"message\":\"serial ota not active\"}\n");
                }
            } else if (ota_service_is_busy()) {
                usb_write_text("APP_ERROR:{\"message\":\"ota busy, only OTA_* commands allowed\"}\n");
            } else if (strcmp(line, "GET_STATUS") == 0) {
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
            } else if (strcmp(line, "GET_LOW_POWER") == 0) {
                emit_json_line("APP_LOW_POWER", device_profile_build_low_power_json);
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
            } else if (strncmp(line, "SET_LOW_POWER ", 14) == 0) {
                char message[96];
                char response[160];
                esp_err_t ret = device_profile_set_low_power_json(line + 14, message, sizeof(message));
                if (ret == ESP_OK) {
                    snprintf(response, sizeof(response), "APP_OK:{\"message\":\"%s\"}\n", message);
                    usb_write_text(response);
                    network_service_set_power_save(device_profile_low_power_enabled());
                    emit_json_line("APP_LOW_POWER", device_profile_build_low_power_json);
                    emit_json_line("APP_CONFIG", device_profile_build_config_json);
                    emit_json_line("APP_STATUS", device_profile_build_status_json);
                    telemetry_app_request_immediate_cycle();
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
            } else if (strcmp(line, "SCAN_I2C") == 0) {
                emit_i2c_scan_line();
            } else if (strcmp(line, "DUMP_BMP180") == 0) {
                char json[768];
                if (bmp180_sensor_build_debug_json(json, sizeof(json)) == ESP_OK) {
                    char response[832];
                    snprintf(response, sizeof(response), "APP_EVENT:{\"type\":\"bmp180_debug\",\"data\":%s}\n", json);
                    usb_write_text(response);
                } else {
                    char response[832];
                    snprintf(response, sizeof(response), "APP_ERROR:{\"message\":\"bmp180 debug failed\",\"data\":%s}\n", json);
                    usb_write_text(response);
                }
            } else if (strcmp(line, "HELP") == 0) {
                usb_write_text("APP_OK:{\"commands\":[\"GET_STATUS\",\"GET_CONFIG\",\"GET_OPTIONS\",\"SET_CONFIG {...}\",\"GET_WIFI_LIST\",\"SET_WIFI_LIST [{...}]\",\"GET_LOW_POWER\",\"SET_LOW_POWER {...}\",\"SCAN_WIFI\",\"SCAN_I2C\",\"DUMP_BMP180\",\"OTA_STATUS\",\"OTA_BEGIN {...}\",\"OTA_WRITE_RAW <size> + raw-bytes\",\"OTA_WRITE <hex>\",\"OTA_FINISH\",\"OTA_ABORT\"]}\n");
            } else if (line[0] != '\0') {
                usb_write_text("APP_ERROR:{\"message\":\"unknown command\"}\n");
            }

            line_len = 0;
            line[0] = '\0';
        }
    }
}

static void console_snapshot_task(void *arg)
{
    (void)arg;
    TickType_t last_full_dump = 0;

    vTaskDelay(pdMS_TO_TICKS(1500));

    while (1) {
        if (ota_service_is_busy()) {
            vTaskDelay(pdMS_TO_TICKS(400));
            continue;
        }

        emit_json_line("APP_STATUS", device_profile_build_status_json);

        if (last_full_dump == 0 || (xTaskGetTickCount() - last_full_dump) >= pdMS_TO_TICKS(8000)) {
            emit_json_line("APP_CONFIG", device_profile_build_config_json);
            emit_json_line("APP_OPTIONS", device_profile_build_options_json);
            emit_json_line("APP_LOW_POWER", device_profile_build_low_power_json);
            emit_wifi_list_line();
            last_full_dump = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t console_service_start(void)
{
    usb_serial_jtag_driver_config_t usb_cfg = {
        .tx_buffer_size = 8192,
        .rx_buffer_size = 16384,
    };
    s_usb_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_usb_lock != NULL, ESP_ERR_NO_MEM, TAG, "usb lock create failed");
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&usb_cfg), TAG, "usb serial install failed");
    BaseType_t ok = xTaskCreate(console_task, "console_task", 12288, NULL, 5, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "console task create failed");
    ok = xTaskCreate(console_snapshot_task, "console_snapshot_task", 6144, NULL, 4, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
