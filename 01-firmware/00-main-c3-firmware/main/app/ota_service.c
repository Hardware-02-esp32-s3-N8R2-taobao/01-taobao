#include "ota_service.h"

#include <stdio.h>
#include <strings.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "device_profile.h"
#include "network_service.h"

#define TAG "ota_service"
#define OTA_NAMESPACE "device_cfg"
#define OTA_HTTP_TIMEOUT_MS 15000
#define OTA_BUFFER_SIZE 1024
#define OTA_CHECK_INTERVAL_MS 60000

typedef struct {
    bool initialized;
    bool busy;
    bool serial_active;
    int64_t last_check_ms;
    char persisted_status[32];
    char job_id[64];
    char target_version[32];
    size_t serial_total_size;
    size_t serial_received_size;
    esp_ota_handle_t serial_handle;
    const esp_partition_t *serial_partition;
    mbedtls_sha256_context serial_sha256;
    char serial_expected_sha256[65];
    char serial_stage[24];
    char serial_message[96];
} ota_state_t;

typedef struct {
    bool has_update;
    bool force;
    char job_id[64];
    char target_version[32];
    char url[256];
} ota_job_t;

static ota_state_t s_state = {0};

static void set_message(char *message, size_t message_size, const char *text)
{
    if (message == NULL || message_size == 0) {
        return;
    }
    snprintf(message, message_size, "%s", text != NULL ? text : "");
}

static void set_serial_status(const char *stage, const char *message)
{
    snprintf(s_state.serial_stage, sizeof(s_state.serial_stage), "%s", stage != NULL ? stage : "");
    snprintf(s_state.serial_message, sizeof(s_state.serial_message), "%s", message != NULL ? message : "");
}

static const char *running_firmware_version(void)
{
    const char *version = device_profile_firmware_version();
    return version != NULL && version[0] != '\0' ? version : APP_FIRMWARE_VERSION;
}

static void load_nvs_string(nvs_handle_t handle, const char *key, char *buffer, size_t buffer_size)
{
    size_t required = buffer_size;
    if (nvs_get_str(handle, key, buffer, &required) != ESP_OK) {
        buffer[0] = '\0';
    }
}

static void save_state_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(OTA_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_str(handle, "ota_status", s_state.persisted_status);
    nvs_set_str(handle, "ota_job", s_state.job_id);
    nvs_set_str(handle, "ota_target", s_state.target_version);
    nvs_commit(handle);
    nvs_close(handle);
}

static void update_persisted_state(const char *status, const char *job_id, const char *target_version)
{
    snprintf(s_state.persisted_status, sizeof(s_state.persisted_status), "%s", status != NULL ? status : "");
    snprintf(s_state.job_id, sizeof(s_state.job_id), "%s", job_id != NULL ? job_id : "");
    snprintf(s_state.target_version, sizeof(s_state.target_version), "%s", target_version != NULL ? target_version : "");
    save_state_to_nvs();
}

static void clear_persisted_state(void)
{
    update_persisted_state("", "", "");
}

static void clear_serial_state(void)
{
    if (s_state.serial_active && s_state.serial_handle != 0) {
        esp_ota_abort(s_state.serial_handle);
    }
    mbedtls_sha256_free(&s_state.serial_sha256);
    s_state.serial_active = false;
    s_state.serial_total_size = 0;
    s_state.serial_received_size = 0;
    s_state.serial_handle = 0;
    s_state.serial_partition = NULL;
    s_state.serial_expected_sha256[0] = '\0';
    set_serial_status("", "");
    s_state.busy = false;
}

static esp_err_t http_post_json(const char *url, const char *json_payload)
{
    char response_buffer[64];
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(client != NULL, ESP_FAIL, TAG, "http client init failed");

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

static esp_err_t report_status(const char *status, const char *message, const char *reported_version, float progress_percent)
{
    char url[256];
    char payload[448];

    snprintf(url, sizeof(url), "%s%s", APP_OTA_SERVER_BASE_URL, APP_OTA_REPORT_PATH);
    snprintf(
        payload,
        sizeof(payload),
        "{\"deviceId\":\"%s\",\"jobId\":\"%s\",\"status\":\"%s\",\"message\":\"%s\",\"fwVersion\":\"%s\",\"targetVersion\":\"%s\",\"progressPercent\":%.1f}",
        device_profile_device_id(),
        s_state.job_id,
        status != NULL ? status : "",
        message != NULL ? message : "",
        reported_version != NULL ? reported_version : running_firmware_version(),
        s_state.target_version,
        progress_percent
    );
    return http_post_json(url, payload);
}

static void maybe_report_boot_outcome(void)
{
    if (!network_service_is_wifi_ready()) {
        return;
    }

    if (strcmp(s_state.persisted_status, "success_pending_report") == 0) {
        if (report_status("success", "new firmware confirmed", running_firmware_version(), 100.0f) == ESP_OK) {
            clear_persisted_state();
        }
    } else if (strcmp(s_state.persisted_status, "rollback_pending_report") == 0) {
        if (report_status("rolled_back", "device rolled back to previous firmware", running_firmware_version(), 0.0f) == ESP_OK) {
            clear_persisted_state();
        }
    }
}

static bool startup_diagnostic_ok(void)
{
    return device_profile_device_id()[0] != '\0';
}

static void process_boot_partition_state(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;

    if (running != NULL && esp_ota_get_state_partition(running, &ota_state) == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (startup_diagnostic_ok()) {
            ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
            update_persisted_state("success_pending_report", s_state.job_id, s_state.target_version);
            return;
        }

        update_persisted_state("rollback_pending_report", s_state.job_id, s_state.target_version);
        esp_ota_mark_app_invalid_rollback_and_reboot();
        return;
    }

    if (strcmp(s_state.persisted_status, "rebooting") == 0 && strcmp(running_firmware_version(), s_state.target_version) != 0) {
        const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();
        if (last_invalid != NULL) {
            update_persisted_state("rollback_pending_report", s_state.job_id, s_state.target_version);
        }
    }
}

static esp_err_t parse_check_response(const char *response, ota_job_t *job)
{
    cJSON *root = cJSON_Parse(response);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_FAIL, TAG, "invalid ota check json");

    cJSON *has_update = cJSON_GetObjectItemCaseSensitive(root, "hasUpdate");
    job->has_update = cJSON_IsTrue(has_update);
    job->force = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "force"));

    cJSON *job_id = cJSON_GetObjectItemCaseSensitive(root, "jobId");
    cJSON *target_version = cJSON_GetObjectItemCaseSensitive(root, "targetVersion");
    cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");

    if (job->has_update) {
        if (!cJSON_IsString(job_id) || !cJSON_IsString(target_version) || !cJSON_IsString(url)) {
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        snprintf(job->job_id, sizeof(job->job_id), "%s", job_id->valuestring);
        snprintf(job->target_version, sizeof(job->target_version), "%s", target_version->valuestring);
        snprintf(job->url, sizeof(job->url), "%s", url->valuestring);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t fetch_update_job(ota_job_t *job)
{
    char url[256];
    char response[768];

    snprintf(
        url,
        sizeof(url),
        "%s%s?deviceId=%s&fwVersion=%s",
        APP_OTA_SERVER_BASE_URL,
        APP_OTA_CHECK_PATH,
        device_profile_device_id(),
        running_firmware_version()
    );

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(client != NULL, ESP_FAIL, TAG, "http client init failed");

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

static esp_err_t perform_http_ota(const ota_job_t *job)
{
    esp_http_client_config_t config = {
        .url = job->url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(client != NULL, ESP_FAIL, TAG, "ota http client init failed");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_RETURN_ON_FALSE(update_partition != NULL, ESP_FAIL, TAG, "no ota partition available");

    esp_ota_handle_t update_handle = 0;
    err = esp_ota_begin(update_partition, content_length > 0 ? content_length : OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    uint8_t buffer[OTA_BUFFER_SIZE];
    int total_read_bytes = 0;
    int last_reported_bucket = -1;
    while (true) {
        int read_bytes = esp_http_client_read(client, (char *)buffer, sizeof(buffer));
        if (read_bytes < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read_bytes == 0) {
            break;
        }

        err = esp_ota_write(update_handle, buffer, read_bytes);
        if (err != ESP_OK) {
            break;
        }

        total_read_bytes += read_bytes;
        if (content_length > 0) {
            float progress_percent = ((float)total_read_bytes * 100.0f) / (float)content_length;
            if (progress_percent > 100.0f) {
                progress_percent = 100.0f;
            }
            int progress_bucket = (int)(progress_percent / 10.0f);
            if (progress_bucket > last_reported_bucket) {
                last_reported_bucket = progress_bucket;
                (void)report_status("downloading", "ota download in progress", running_firmware_version(), progress_percent);
            }
        }
    }

    if (err == ESP_OK) {
        err = esp_ota_end(update_handle);
    } else {
        esp_ota_abort(update_handle);
    }

    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(update_partition);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t ota_service_init(void)
{
    nvs_handle_t handle;

    if (nvs_open(OTA_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        load_nvs_string(handle, "ota_status", s_state.persisted_status, sizeof(s_state.persisted_status));
        load_nvs_string(handle, "ota_job", s_state.job_id, sizeof(s_state.job_id));
        load_nvs_string(handle, "ota_target", s_state.target_version, sizeof(s_state.target_version));
        nvs_close(handle);
    }

    process_boot_partition_state();
    s_state.initialized = true;
    return ESP_OK;
}

void ota_service_process(void)
{
    ota_job_t job = {0};
    esp_err_t err;
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (!s_state.initialized || s_state.busy || !network_service_is_wifi_ready()) {
        maybe_report_boot_outcome();
        return;
    }
    if (s_state.last_check_ms > 0 && (now_ms - s_state.last_check_ms) < OTA_CHECK_INTERVAL_MS) {
        maybe_report_boot_outcome();
        return;
    }

    maybe_report_boot_outcome();

    s_state.busy = true;
    s_state.last_check_ms = now_ms;
    err = fetch_update_job(&job);
    if (err != ESP_OK) {
        s_state.busy = false;
        return;
    }

    if (!job.has_update) {
        s_state.busy = false;
        return;
    }

    update_persisted_state("downloading", job.job_id, job.target_version);
    (void)report_status("downloading", "ota download started", running_firmware_version(), 0.0f);
    err = perform_http_ota(&job);
    if (err != ESP_OK) {
        (void)report_status("failed", esp_err_to_name(err), running_firmware_version(), 0.0f);
        clear_persisted_state();
        s_state.busy = false;
        return;
    }

    update_persisted_state("rebooting", job.job_id, job.target_version);
    (void)report_status("rebooting", "ota image written, rebooting", running_firmware_version(), 100.0f);
    esp_restart();
}

bool ota_service_should_skip_sleep(void)
{
    return s_state.busy;
}

bool ota_service_is_busy(void)
{
    return s_state.busy;
}

static bool is_hex_char(char ch)
{
    return (ch >= '0' && ch <= '9')
        || (ch >= 'a' && ch <= 'f')
        || (ch >= 'A' && ch <= 'F');
}

static uint8_t hex_char_to_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (uint8_t)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (uint8_t)(10 + ch - 'a');
    }
    return (uint8_t)(10 + ch - 'A');
}

static void format_sha256_hex(const uint8_t digest[32], char out[65])
{
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        out[i * 2] = hex[(digest[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    out[64] = '\0';
}

esp_err_t ota_service_serial_begin(
    size_t image_size,
    const char *expected_sha256,
    const char *target_version,
    char *message,
    size_t message_size
)
{
    if (message != NULL && message_size > 0) {
        message[0] = '\0';
    }

    if (s_state.busy) {
        set_message(message, message_size, "ota busy");
        return ESP_ERR_INVALID_STATE;
    }
    if (image_size == 0) {
        set_message(message, message_size, "image size invalid");
        return ESP_ERR_INVALID_ARG;
    }
    if (expected_sha256 != NULL && expected_sha256[0] != '\0' && strlen(expected_sha256) != 64) {
        set_message(message, message_size, "sha256 length invalid");
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        set_message(message, message_size, "no ota partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, image_size, &handle);
    if (err != ESP_OK) {
        if (message != NULL && message_size > 0) {
            snprintf(message, message_size, "ota begin failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    memset(&s_state.serial_sha256, 0, sizeof(s_state.serial_sha256));
    mbedtls_sha256_init(&s_state.serial_sha256);
    if (mbedtls_sha256_starts(&s_state.serial_sha256, 0) != 0) {
        esp_ota_abort(handle);
        mbedtls_sha256_free(&s_state.serial_sha256);
        set_message(message, message_size, "sha256 init failed");
        return ESP_FAIL;
    }

    s_state.busy = true;
    s_state.serial_active = true;
    s_state.serial_total_size = image_size;
    s_state.serial_received_size = 0;
    s_state.serial_handle = handle;
    s_state.serial_partition = update_partition;
    snprintf(s_state.serial_expected_sha256, sizeof(s_state.serial_expected_sha256), "%s", expected_sha256 != NULL ? expected_sha256 : "");
    snprintf(s_state.target_version, sizeof(s_state.target_version), "%s", target_version != NULL ? target_version : "");
    set_serial_status("ready", "serial ota ready");
    set_message(message, message_size, "ready");
    return ESP_OK;
}

esp_err_t ota_service_serial_write_hex(const char *hex_payload, size_t *written_size, char *message, size_t message_size)
{
    uint8_t binary[256];
    size_t hex_len;
    size_t byte_len;

    if (written_size != NULL) {
        *written_size = 0;
    }
    if (message != NULL && message_size > 0) {
        message[0] = '\0';
    }

    if (!s_state.serial_active || s_state.serial_handle == 0) {
        set_message(message, message_size, "serial ota not active");
        return ESP_ERR_INVALID_STATE;
    }
    if (hex_payload == NULL) {
        set_message(message, message_size, "empty payload");
        return ESP_ERR_INVALID_ARG;
    }

    hex_len = strlen(hex_payload);
    if (hex_len == 0 || (hex_len % 2) != 0 || hex_len > sizeof(binary) * 2) {
        set_message(message, message_size, "hex payload invalid");
        return ESP_ERR_INVALID_ARG;
    }

    byte_len = hex_len / 2;
    if ((s_state.serial_received_size + byte_len) > s_state.serial_total_size) {
        ota_service_serial_abort("image larger than declared size", NULL, 0);
        set_message(message, message_size, "image larger than declared size");
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < hex_len; i += 2) {
        if (!is_hex_char(hex_payload[i]) || !is_hex_char(hex_payload[i + 1])) {
            set_message(message, message_size, "hex payload invalid");
            return ESP_ERR_INVALID_ARG;
        }
        binary[i / 2] = (uint8_t)((hex_char_to_nibble(hex_payload[i]) << 4) | hex_char_to_nibble(hex_payload[i + 1]));
    }

    esp_err_t err = esp_ota_write(s_state.serial_handle, binary, byte_len);
    if (err != ESP_OK) {
        ota_service_serial_abort("ota write failed", NULL, 0);
        if (message != NULL && message_size > 0) {
            snprintf(message, message_size, "ota write failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    if (mbedtls_sha256_update(&s_state.serial_sha256, binary, byte_len) != 0) {
        ota_service_serial_abort("sha256 update failed", NULL, 0);
        set_message(message, message_size, "sha256 update failed");
        return ESP_FAIL;
    }

    s_state.serial_received_size += byte_len;
    set_serial_status("receiving", "serial ota receiving");
    if (written_size != NULL) {
        *written_size = byte_len;
    }
    if (message != NULL && message_size > 0) {
        snprintf(message, message_size, "received %u/%u bytes", (unsigned)s_state.serial_received_size, (unsigned)s_state.serial_total_size);
    }
    return ESP_OK;
}

esp_err_t ota_service_serial_finish(char *message, size_t message_size)
{
    uint8_t digest[32];
    char digest_hex[65];
    esp_err_t err;

    if (message != NULL && message_size > 0) {
        message[0] = '\0';
    }
    if (!s_state.serial_active || s_state.serial_handle == 0) {
        set_message(message, message_size, "serial ota not active");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state.serial_received_size != s_state.serial_total_size) {
        set_message(message, message_size, "image incomplete");
        return ESP_ERR_INVALID_SIZE;
    }

    if (mbedtls_sha256_finish(&s_state.serial_sha256, digest) != 0) {
        ota_service_serial_abort("sha256 finish failed", NULL, 0);
        set_message(message, message_size, "sha256 finish failed");
        return ESP_FAIL;
    }
    format_sha256_hex(digest, digest_hex);

    if (s_state.serial_expected_sha256[0] != '\0' && strcasecmp(digest_hex, s_state.serial_expected_sha256) != 0) {
        ota_service_serial_abort("sha256 mismatch", NULL, 0);
        set_message(message, message_size, "sha256 mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    err = esp_ota_end(s_state.serial_handle);
    if (err != ESP_OK) {
        ota_service_serial_abort("ota end failed", NULL, 0);
        if (message != NULL && message_size > 0) {
            snprintf(message, message_size, "ota end failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    s_state.serial_handle = 0;
    err = esp_ota_set_boot_partition(s_state.serial_partition);
    if (err != ESP_OK) {
        ota_service_serial_abort("set boot partition failed", NULL, 0);
        if (message != NULL && message_size > 0) {
            snprintf(message, message_size, "set boot partition failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    update_persisted_state("rebooting", "serial-local", s_state.target_version[0] != '\0' ? s_state.target_version : running_firmware_version());
    set_serial_status("done", "serial ota image ready");
    set_message(message, message_size, "done");
    mbedtls_sha256_free(&s_state.serial_sha256);
    s_state.serial_active = false;
    s_state.busy = false;
    return ESP_OK;
}

esp_err_t ota_service_serial_abort(const char *reason, char *message, size_t message_size)
{
    const bool was_active = s_state.serial_active;
    clear_serial_state();
    if (message != NULL && message_size > 0) {
        snprintf(message, message_size, "%s", reason != NULL && reason[0] != '\0' ? reason : "serial ota aborted");
    }
    return was_active ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void ota_service_build_serial_status_json(char *buffer, size_t buffer_size)
{
    unsigned progress = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (s_state.serial_total_size > 0) {
        progress = (unsigned)((s_state.serial_received_size * 100U) / s_state.serial_total_size);
    }
    snprintf(
        buffer,
        buffer_size,
        "{\"active\":%s,\"state\":\"%s\",\"received\":%u,\"total\":%u,\"progress\":%u,\"targetVersion\":\"%s\",\"message\":\"%s\"}",
        s_state.serial_active ? "true" : "false",
        s_state.serial_stage,
        (unsigned)s_state.serial_received_size,
        (unsigned)s_state.serial_total_size,
        progress,
        s_state.target_version,
        s_state.serial_message
    );
}
