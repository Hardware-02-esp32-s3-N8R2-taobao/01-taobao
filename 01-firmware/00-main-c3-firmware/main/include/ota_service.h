#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t ota_service_init(void);
void ota_service_process(void);
bool ota_service_should_skip_sleep(void);
bool ota_service_is_busy(void);
esp_err_t ota_service_serial_begin(
    size_t image_size,
    const char *expected_sha256,
    const char *target_version,
    char *message,
    size_t message_size
);
esp_err_t ota_service_serial_write_binary(const uint8_t *payload, size_t payload_size, size_t *written_size, char *message, size_t message_size);
esp_err_t ota_service_serial_write_hex(const char *hex_payload, size_t *written_size, char *message, size_t message_size);
esp_err_t ota_service_serial_finish(char *message, size_t message_size);
esp_err_t ota_service_serial_abort(const char *reason, char *message, size_t message_size);
void ota_service_build_serial_status_json(char *buffer, size_t buffer_size);
