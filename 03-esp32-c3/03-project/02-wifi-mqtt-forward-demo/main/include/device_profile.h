#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct {
    bool ready;
    float temperature_c;
    float humidity_pct;
    int rssi;
    char ip[16];
    char payload[256];
} publish_snapshot_t;

esp_err_t device_profile_init(void);

const char *device_profile_device_id(void);
const char *device_profile_device_name(void);
const char *device_profile_device_alias(void);
const char *device_profile_device_source(void);

void device_profile_update_wifi(bool connected, const char *ip, int disconnect_reason);
void device_profile_update_mqtt(bool connected);
void device_profile_update_dht11(bool ready, float temperature_c, float humidity_pct);
void device_profile_update_publish(bool ready, float temperature_c, float humidity_pct, int rssi, const char *ip, const char *payload);

void device_profile_build_config_json(char *buffer, size_t buffer_size);
void device_profile_build_status_json(char *buffer, size_t buffer_size);
void device_profile_build_options_json(char *buffer, size_t buffer_size);
esp_err_t device_profile_apply_config_json(const char *json_text, char *message, size_t message_size);
