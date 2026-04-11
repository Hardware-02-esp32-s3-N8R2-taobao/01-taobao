#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t ota_service_init(void);
void ota_service_process(void);
bool ota_service_should_skip_sleep(void);
