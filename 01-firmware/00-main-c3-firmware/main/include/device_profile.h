#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool ready;
    float temperature_c;
    float humidity_pct;
    int rssi;
    char payload[512];
} publish_snapshot_t;

typedef enum {
    DEVICE_HW_VARIANT_UNKNOWN = 0,
    DEVICE_HW_VARIANT_SUPERMINI,
    DEVICE_HW_VARIANT_OLED_SCREEN,
} device_hw_variant_t;

esp_err_t device_profile_init(void);

const char *device_profile_device_name(void);
const char *device_profile_device_id(void);
const char *device_profile_device_alias(void);
const char *device_profile_firmware_version(void);
const char *device_profile_mqtt_topic(void);
device_hw_variant_t device_profile_hardware_variant(void);
const char *device_profile_hardware_variant_name(void);

void device_profile_update_wifi(bool connected, const char *ssid, const char *ip, int disconnect_reason);
void device_profile_update_mqtt(bool connected);
void device_profile_update_dht11(bool ready, float temperature_c, float humidity_pct);
void device_profile_update_sensor_snapshot(const char *json_text, int ready_count, int total_count);
void device_profile_update_publish(bool ready, int rssi, const char *payload);
bool device_profile_has_sensor(const char *sensor_type);
void device_profile_copy_sensors_csv(char *buffer, size_t buffer_size);

void device_profile_build_config_json(char *buffer, size_t buffer_size);
void device_profile_build_status_json(char *buffer, size_t buffer_size);
void device_profile_build_options_json(char *buffer, size_t buffer_size);
esp_err_t device_profile_apply_config_json(const char *json_text, char *message, size_t message_size);

int  device_profile_get_wifi_count(void);
bool device_profile_get_wifi_entry(int index, char *ssid, size_t ssid_size, char *password, size_t pw_size);
void device_profile_get_wifi_list_json(char *buffer, size_t buffer_size);
esp_err_t device_profile_set_wifi_list_json(const char *json_text, char *message, size_t message_size);
esp_err_t device_profile_replace_wifi_credential(const char *ssid, const char *password, char *message, size_t message_size);

bool device_profile_low_power_enabled(void);
uint32_t device_profile_low_power_interval_sec(void);
bool device_profile_maintenance_mode_enabled(void);
bool device_profile_should_enable_wifi_power_save(void);
void device_profile_set_low_power_state(bool enabled, uint32_t interval_sec);
void device_profile_build_low_power_json(char *buffer, size_t buffer_size);
esp_err_t device_profile_set_low_power_json(const char *json_text, char *message, size_t message_size);
