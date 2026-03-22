#pragma once

#include "esp_err.h"

esp_err_t console_service_start(void);
void console_service_emit_event(const char *event_type, const char *json_payload);
