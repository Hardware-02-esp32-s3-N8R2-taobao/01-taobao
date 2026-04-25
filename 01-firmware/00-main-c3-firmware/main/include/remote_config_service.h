#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    bool received;
    bool has_job;
    bool sleep_approved;
    char publish_id[48];
    char job_id[80];
    char config_json[512];
    char low_power_json[160];
    char message[160];
    char sleep_reason[80];
    char server_mode[24];
} remote_config_ack_t;

esp_err_t remote_config_service_init(void);
void remote_config_service_process(void);
void remote_config_service_prepare_for_publish(const char *publish_id);
bool remote_config_service_wait_for_ack(int timeout_ms, remote_config_ack_t *ack);
void remote_config_service_handle_mqtt_message(const char *topic, const char *payload, int payload_len);
esp_err_t remote_config_service_apply_ack_job(const remote_config_ack_t *ack, char *message, size_t message_size);
