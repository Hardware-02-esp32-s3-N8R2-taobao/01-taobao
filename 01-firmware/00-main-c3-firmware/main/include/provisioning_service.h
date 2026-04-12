#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t provisioning_service_init(void);
esp_err_t provisioning_service_start(void);
bool provisioning_service_is_active(void);
bool provisioning_service_is_transitioning(void);
