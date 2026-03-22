#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct {
    bool ready;
    float temperature_c;
    float humidity_pct;
    int rssi;
    char payload[512];
} publish_snapshot_t;

esp_err_t device_profile_init(void);

const char *device_profile_device_name(void);
const char *device_profile_device_id(void);
const char *device_profile_device_alias(void);

void device_profile_update_wifi(bool connected, const char *ssid, const char *ip, int disconnect_reason);
void device_profile_update_mqtt(bool connected);
void device_profile_update_dht11(bool ready, float temperature_c, float humidity_pct);
void device_profile_update_publish(bool ready, float temperature_c, float humidity_pct, int rssi, const char *payload);

void device_profile_build_config_json(char *buffer, size_t buffer_size);
void device_profile_build_status_json(char *buffer, size_t buffer_size);
void device_profile_build_options_json(char *buffer, size_t buffer_size);
esp_err_t device_profile_apply_config_json(const char *json_text, char *message, size_t message_size);

int  device_profile_get_wifi_count(void);
bool device_profile_get_wifi_entry(int index, char *ssid, size_t ssid_size, char *password, size_t pw_size);
void device_profile_get_wifi_list_json(char *buffer, size_t buffer_size);
esp_err_t device_profile_set_wifi_list_json(const char *json_text, char *message, size_t message_size);
