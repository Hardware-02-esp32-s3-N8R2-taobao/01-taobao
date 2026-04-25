#include "remote_config_service.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "app_config.h"
#include "device_profile.h"
#include "network_service.h"

#define TAG "remote_cfg"
#define REMOTE_CONFIG_HTTP_TIMEOUT_MS 15000
#define REMOTE_CONFIG_ACK_READY_BIT BIT0

typedef struct {
    char expected_publish_id[48];
    remote_config_ack_t last_ack;
} remote_config_state_t;

static remote_config_state_t s_state = {0};
static EventGroupHandle_t s_event_group = NULL;
static SemaphoreHandle_t s_lock = NULL;

static bool is_ack_topic(const char *topic)
{
    char prefix[64];
    size_t topic_len = 0;
    size_t prefix_len = 0;

    if (topic == NULL) {
        return false;
    }

    snprintf(prefix, sizeof(prefix), "%s/", APP_MQTT_TOPIC_PREFIX);
    topic_len = strlen(topic);
    prefix_len = strlen(prefix);
    if (topic_len <= prefix_len + 4U) {
        return false;
    }
    if (strncmp(topic, prefix, prefix_len) != 0) {
        return false;
    }
    return strcmp(topic + topic_len - 4, "/ack") == 0;
}

static esp_err_t http_post_json(const char *url, const char *json_payload)
{
    char response_buffer[256] = {0};
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = REMOTE_CONFIG_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_open(client, json_payload != NULL ? (int)strlen(json_payload) : 0);
    if (err == ESP_OK && json_payload != NULL) {
        int written = esp_http_client_write(client, json_payload, (int)strlen(json_payload));
        if (written < 0) {
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK) {
        int status_code = 0;
        (void)esp_http_client_fetch_headers(client);
        (void)esp_http_client_read_response(client, response_buffer, sizeof(response_buffer) - 1);
        status_code = esp_http_client_get_status_code(client);
        if (status_code < 200 || status_code >= 300) {
            err = ESP_FAIL;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t report_status(const remote_config_ack_t *ack, const char *status, const char *message)
{
    char url[192];
    char config_json[512];
    char low_power_json[160];
    char payload[1152];

    device_profile_build_config_json(config_json, sizeof(config_json));
    device_profile_build_low_power_json(low_power_json, sizeof(low_power_json));
    snprintf(url, sizeof(url), "%s%s", APP_OTA_SERVER_BASE_URL, APP_REMOTE_CONFIG_REPORT_PATH);
    snprintf(
        payload,
        sizeof(payload),
        "{\"deviceId\":\"%s\",\"jobId\":\"%s\",\"status\":\"%s\",\"message\":\"%s\",\"fwVersion\":\"%s\",\"config\":%s,\"lowPower\":%s}",
        device_profile_device_id(),
        ack != NULL ? ack->job_id : "",
        status != NULL ? status : "unknown",
        message != NULL ? message : "",
        APP_FIRMWARE_VERSION,
        config_json,
        low_power_json
    );
    return http_post_json(url, payload);
}

static esp_err_t parse_ack_payload(const char *payload, remote_config_ack_t *ack)
{
    cJSON *root = NULL;
    cJSON *publish_id = NULL;
    cJSON *job_obj = NULL;
    cJSON *job_id = NULL;
    cJSON *config = NULL;
    cJSON *low_power = NULL;
    cJSON *message = NULL;
    cJSON *sleep_approved = NULL;
    cJSON *sleep_reason = NULL;
    cJSON *server_mode = NULL;

    if (payload == NULL || ack == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_Parse(payload);
    if (root == NULL) {
        return ESP_FAIL;
    }

    publish_id = cJSON_GetObjectItemCaseSensitive(root, "publishId");
    if (!cJSON_IsString(publish_id) || publish_id->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(ack, 0, sizeof(*ack));
    ack->received = true;
    snprintf(ack->publish_id, sizeof(ack->publish_id), "%s", publish_id->valuestring);
    sleep_approved = cJSON_GetObjectItemCaseSensitive(root, "sleepApproved");
    sleep_reason = cJSON_GetObjectItemCaseSensitive(root, "sleepReason");
    server_mode = cJSON_GetObjectItemCaseSensitive(root, "serverMode");
    ack->sleep_approved =
        cJSON_IsTrue(sleep_approved) ||
        (cJSON_IsNumber(sleep_approved) && sleep_approved->valuedouble != 0);
    if (cJSON_IsString(sleep_reason)) {
        snprintf(ack->sleep_reason, sizeof(ack->sleep_reason), "%s", sleep_reason->valuestring);
    }
    if (cJSON_IsString(server_mode)) {
        snprintf(ack->server_mode, sizeof(ack->server_mode), "%s", server_mode->valuestring);
    }

    job_obj = cJSON_GetObjectItemCaseSensitive(root, "configJob");
    if (!cJSON_IsObject(job_obj)) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    job_id = cJSON_GetObjectItemCaseSensitive(job_obj, "id");
    config = cJSON_GetObjectItemCaseSensitive(job_obj, "config");
    low_power = cJSON_GetObjectItemCaseSensitive(job_obj, "lowPower");
    message = cJSON_GetObjectItemCaseSensitive(job_obj, "message");
    if (!cJSON_IsString(job_id) || !cJSON_IsObject(config)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    ack->has_job = true;
    snprintf(ack->job_id, sizeof(ack->job_id), "%s", job_id->valuestring);

    char *config_text = cJSON_PrintUnformatted(config);
    snprintf(ack->config_json, sizeof(ack->config_json), "%s", config_text != NULL ? config_text : "{}");
    cJSON_free(config_text);

    if (cJSON_IsObject(low_power)) {
        char *low_power_text = cJSON_PrintUnformatted(low_power);
        snprintf(ack->low_power_json, sizeof(ack->low_power_json), "%s", low_power_text != NULL ? low_power_text : "{}");
        cJSON_free(low_power_text);
    } else {
        snprintf(ack->low_power_json, sizeof(ack->low_power_json), "%s", "{}");
    }

    if (cJSON_IsString(message)) {
        snprintf(ack->message, sizeof(ack->message), "%s", message->valuestring);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t remote_config_service_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    if (s_event_group == NULL) {
        s_event_group = xEventGroupCreate();
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    return (s_event_group != NULL && s_lock != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

void remote_config_service_process(void)
{
    /* Ack-driven flow: keep the legacy entrypoint as a no-op for compatibility. */
}

void remote_config_service_prepare_for_publish(const char *publish_id)
{
    if (s_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        memset(&s_state.last_ack, 0, sizeof(s_state.last_ack));
        snprintf(
            s_state.expected_publish_id,
            sizeof(s_state.expected_publish_id),
            "%s",
            publish_id != NULL ? publish_id : ""
        );
        xSemaphoreGive(s_lock);
    }

    if (s_event_group != NULL) {
        xEventGroupClearBits(s_event_group, REMOTE_CONFIG_ACK_READY_BIT);
    }
}

bool remote_config_service_wait_for_ack(int timeout_ms, remote_config_ack_t *ack)
{
    EventBits_t bits = 0;

    if (s_event_group == NULL || s_lock == NULL) {
        return false;
    }

    bits = xEventGroupWaitBits(
        s_event_group,
        REMOTE_CONFIG_ACK_READY_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 0)
    );
    if ((bits & REMOTE_CONFIG_ACK_READY_BIT) == 0) {
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    if (ack != NULL) {
        *ack = s_state.last_ack;
    }
    xSemaphoreGive(s_lock);
    return true;
}

void remote_config_service_handle_mqtt_message(const char *topic, const char *payload, int payload_len)
{
    char payload_copy[1024];
    remote_config_ack_t parsed_ack = {0};
    esp_err_t err = ESP_FAIL;

    if (!is_ack_topic(topic) || payload == NULL || payload_len <= 0 || payload_len >= (int)sizeof(payload_copy)) {
        return;
    }
    if (s_lock == NULL || s_event_group == NULL) {
        return;
    }

    memcpy(payload_copy, payload, (size_t)payload_len);
    payload_copy[payload_len] = '\0';
    err = parse_ack_payload(payload_copy, &parsed_ack);
    if (err != ESP_OK || !parsed_ack.received) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (
        s_state.expected_publish_id[0] == '\0' ||
        strcmp(parsed_ack.publish_id, s_state.expected_publish_id) != 0
    ) {
        xSemaphoreGive(s_lock);
        return;
    }
    s_state.last_ack = parsed_ack;
    xSemaphoreGive(s_lock);

    xEventGroupSetBits(s_event_group, REMOTE_CONFIG_ACK_READY_BIT);
}

esp_err_t remote_config_service_apply_ack_job(const remote_config_ack_t *ack, char *message, size_t message_size)
{
    esp_err_t err = ESP_OK;
    char apply_message[128] = {0};

    if (ack == NULL || !ack->has_job || ack->job_id[0] == '\0') {
        snprintf(message, message_size, "no config job in ack");
        return ESP_ERR_NOT_FOUND;
    }

    err = device_profile_apply_config_json(ack->config_json, apply_message, sizeof(apply_message));
    if (err != ESP_OK) {
        (void)report_status(ack, "failed", apply_message[0] != '\0' ? apply_message : "config apply failed");
        snprintf(message, message_size, "%s", apply_message[0] != '\0' ? apply_message : "config apply failed");
        return err;
    }

    err = device_profile_set_low_power_json(ack->low_power_json, apply_message, sizeof(apply_message));
    if (err != ESP_OK) {
        (void)report_status(ack, "failed", apply_message[0] != '\0' ? apply_message : "low power apply failed");
        snprintf(message, message_size, "%s", apply_message[0] != '\0' ? apply_message : "low power apply failed");
        return err;
    }

    network_service_set_power_save(device_profile_should_enable_wifi_power_save());
    err = report_status(ack, "success", ack->message[0] != '\0' ? ack->message : "config applied");
    if (err != ESP_OK) {
        snprintf(message, message_size, "config applied but report failed");
        return err;
    }

    snprintf(message, message_size, "%s", ack->message[0] != '\0' ? ack->message : "config applied");
    return ESP_OK;
}
