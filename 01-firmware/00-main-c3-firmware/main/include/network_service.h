#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t network_service_start(void);
bool network_service_is_wifi_ready(void);
bool network_service_is_mqtt_ready(void);
int network_service_get_rssi(void);
const char *network_service_get_ip(void);
esp_err_t network_service_publish_json(const char *topic, const char *json_payload);
bool network_service_wait_for_publish(int timeout_ms);
void network_service_reload_wifi_list(void);
void network_service_get_scan_json(char *buffer, size_t buffer_size);
void network_service_set_power_save(bool enabled);
void network_service_prepare_for_sleep(void);
