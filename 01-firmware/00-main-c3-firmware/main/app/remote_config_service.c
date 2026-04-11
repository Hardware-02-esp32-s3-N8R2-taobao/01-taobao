#include "remote_config_service.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "device_profile.h"

#define TAG "remote_cfg"
#define REMOTE_CONFIG_HTTP_TIMEOUT_MS 15000
#define REMOTE_CONFIG_CHECK_INTERVAL_MS 10000

typedef struct {
    int64_t last_check_ms;
} remote_config_state_t;

typedef struct {
    char job_id[80];
    char config_json[512];
    char low_power_json[160];
    char message[160];
} remote_config_job_t;

static remote_config_state_t s_state = {0};

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
        (void)esp_http_client_fetch_headers(client);
        (void)esp_http_client_read_response(client, response_buffer, sizeof(response_buffer) - 1);
        int status_code = esp_http_client_get_status_code(client);
        if (status_code < 200 || status_code >= 300) {
            err = ESP_FAIL;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t report_status(const remote_config_job_t *job, const char *status, const char *message)
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
        job != NULL ? job->job_id : "",
        status != NULL ? status : "unknown",
        message != NULL ? message : "",
        APP_FIRMWARE_VERSION,
        config_json,
        low_power_json
    );
    return http_post_json(url, payload);
}

static esp_err_t parse_check_response(const char *response, remote_config_job_t *job)
{
    cJSON *root = cJSON_Parse(response);
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *has_update = cJSON_GetObjectItemCaseSensitive(root, "hasUpdate");
    if (!cJSON_IsBool(has_update) || !cJSON_IsTrue(has_update)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *job_obj = cJSON_GetObjectItemCaseSensitive(root, "job");
    cJSON *job_id = cJSON_GetObjectItemCaseSensitive(job_obj, "id");
    cJSON *config = cJSON_GetObjectItemCaseSensitive(job_obj, "config");
    cJSON *low_power = cJSON_GetObjectItemCaseSensitive(job_obj, "lowPower");
    cJSON *message = cJSON_GetObjectItemCaseSensitive(job_obj, "message");

    if (!cJSON_IsObject(job_obj) || !cJSON_IsString(job_id) || !cJSON_IsObject(config)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(job, 0, sizeof(*job));
    snprintf(job->job_id, sizeof(job->job_id), "%s", job_id->valuestring);

    char *config_text = cJSON_PrintUnformatted(config);
    snprintf(job->config_json, sizeof(job->config_json), "%s", config_text != NULL ? config_text : "{}");
    cJSON_free(config_text);

    if (cJSON_IsObject(low_power)) {
        char *low_power_text = cJSON_PrintUnformatted(low_power);
        snprintf(job->low_power_json, sizeof(job->low_power_json), "%s", low_power_text != NULL ? low_power_text : "{}");
        cJSON_free(low_power_text);
    } else {
        snprintf(job->low_power_json, sizeof(job->low_power_json), "%s", "{}");
    }

    if (cJSON_IsString(message)) {
        snprintf(job->message, sizeof(job->message), "%s", message->valuestring);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t fetch_update_job(remote_config_job_t *job)
{
    char url[256];
    char response[1024] = {0};
    snprintf(
        url,
        sizeof(url),
        "%s%s?deviceId=%s&fwVersion=%s",
        APP_OTA_SERVER_BASE_URL,
        APP_REMOTE_CONFIG_CHECK_PATH,
        device_profile_device_id(),
        APP_FIRMWARE_VERSION
    );

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = REMOTE_CONFIG_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    (void)esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int read_len = esp_http_client_read_response(client, response, sizeof(response) - 1);
    if (read_len < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    response[read_len] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return parse_check_response(response, job);
}

esp_err_t remote_config_service_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    return ESP_OK;
}

void remote_config_service_process(void)
{
    remote_config_job_t job = {0};
    char message[128] = {0};
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (s_state.last_check_ms > 0 && (now_ms - s_state.last_check_ms) < REMOTE_CONFIG_CHECK_INTERVAL_MS) {
        return;
    }
    s_state.last_check_ms = now_ms;

    esp_err_t err = fetch_update_job(&job);
    if (err == ESP_ERR_NOT_FOUND) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config check failed: %s", esp_err_to_name(err));
        return;
    }

    err = device_profile_apply_config_json(job.config_json, message, sizeof(message));
    if (err != ESP_OK) {
        (void)report_status(&job, "failed", message[0] != '\0' ? message : "config apply failed");
        return;
    }

    err = device_profile_set_low_power_json(job.low_power_json, message, sizeof(message));
    if (err != ESP_OK) {
        (void)report_status(&job, "failed", message[0] != '\0' ? message : "low power apply failed");
        return;
    }

    (void)report_status(&job, "success", job.message[0] != '\0' ? job.message : "config applied");
}
