#ifndef PUMP_CONTROLLER_H
#define PUMP_CONTROLLER_H

#include <stdint.h>

#include "esp_err.h"

#include "app/app_types.h"

esp_err_t pump_controller_init(void);
void pump_controller_start(uint32_t duration_seconds, const char *requested_by, const char *issued_at);
void pump_controller_stop(const char *requested_by, const char *issued_at);
void pump_controller_tick(void);
void pump_controller_get_state(pump_state_t *state);

#endif
