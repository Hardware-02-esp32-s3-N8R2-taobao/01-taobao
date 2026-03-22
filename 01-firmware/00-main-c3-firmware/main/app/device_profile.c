#include "device_profile.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app_config.h"

#define TAG "device_profile"
#define DEVICE_PROFILE_NAMESPACE "device_cfg"
#define WIFI_MAX_ENTRIES 8

typedef struct {
    char ssid[33];
    char password[65];
} wifi_entry_t;

typedef struct {
    const char *name;
    const char *device_id;
    const char *device_alias;
} device_option_t;

static wifi_entry_t s_wifi_entries[WIFI_MAX_ENTRIES];
static int s_wifi_count = 0;

#define DEFAULT_DEVICE_NAME   "庭院1号"
#define DEFAULT_SENSOR_LIST   "dht11"

typedef struct {
    char device_name[32];
    char sensors_csv[96];
    char fw_version[32];
    bool wifi_connected;
    char wifi_ssid[33];
    char wifi_ip[16];
    int wifi_disconnect_reason;
    bool mqtt_connected;
    bool dht11_ready;
    float dht11_temperature_c;
    float dht11_humidity_pct;
    publish_snapshot_t publish;
    char chip_model[24];
    char target[16];
    char mac[18];
    int chip_cores;
    int chip_revision;
} device_profile_state_t;

static device_profile_state_t s_state;
static SemaphoreHandle_t s_lock;

static const device_option_t s_device_options[] = {
    { "庭院1号", "yard-01", "庭院 1 号" },
    { "卧室1号", "bedroom-01", "卧室 1 号" },
    { "书房1号", "study-01", "书房 1 号" },
    { "办公室1号", "office-01", "办公室 1 号" },
};

static const char *s_sensor_type_options[] = {
    "dht11",
    "ds18b20",
    "bh1750",
    "bmp180",
    "bmp280",
    "bme280",
    "soil_moisture",
    "rain_sensor",
};

static const device_option_t *find_device_option(const char *device_name)
{
    if (device_name == NULL || device_name[0] == '\0') {
        return &s_device_options[0];
    }

    for (size_t i = 0; i < sizeof(s_device_options) / sizeof(s_device_options[0]); i++) {
        if (strcmp(s_device_options[i].name, device_name) == 0) {
            return &s_device_options[i];
        }
    }

    return &s_device_options[0];
}

static void profile_lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void profile_unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void set_default_strings(void)
{
    snprintf(s_state.device_name, sizeof(s_state.device_name), "%s", DEFAULT_DEVICE_NAME);
    snprintf(s_state.sensors_csv, sizeof(s_state.sensors_csv), "%s", DEFAULT_SENSOR_LIST);
    snprintf(s_state.fw_version, sizeof(s_state.fw_version), "%s", APP_FIRMWARE_VERSION);
    snprintf(s_state.wifi_ssid, sizeof(s_state.wifi_ssid), "--");
    snprintf(s_state.wifi_ip, sizeof(s_state.wifi_ip), "0.0.0.0");
    snprintf(s_state.publish.payload, sizeof(s_state.publish.payload), "{}");
}

static void load_nvs_string(nvs_handle_t handle, const char *key, char *buffer, size_t buffer_size)
{
    size_t required_size = buffer_size;
    if (nvs_get_str(handle, key, buffer, &required_size) != ESP_OK) {
        return;
    }
}

static void save_nvs_strings(void)
{
    nvs_handle_t handle;
    if (nvs_open(DEVICE_PROFILE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_str(handle, "device_name", s_state.device_name);
    nvs_set_str(handle, "sensors_csv", s_state.sensors_csv);
    nvs_commit(handle);
    nvs_close(handle);
}

static const char *chip_model_to_text(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32C2:
        return "ESP32-C2";
    case CHIP_ESP32C6:
        return "ESP32-C6";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    default:
        return "ESP32";
    }
}

static void populate_hardware_info(void)
{
    esp_chip_info_t chip_info = {0};
    uint8_t mac[6] = {0};

    esp_chip_info(&chip_info);
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(s_state.chip_model, sizeof(s_state.chip_model), "%s", chip_model_to_text(chip_info.model));
    snprintf(s_state.target, sizeof(s_state.target), "%s", CONFIG_IDF_TARGET);
    snprintf(
        s_state.mac,
        sizeof(s_state.mac),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );
    s_state.chip_cores = chip_info.cores;
    s_state.chip_revision = chip_info.revision;
}

static void append_json_string_array(cJSON *parent, const char *name, const char *csv)
{
    cJSON *array = cJSON_AddArrayToObject(parent, name);
    char local_copy[96];
    snprintf(local_copy, sizeof(local_copy), "%s", csv);

    char *token = strtok(local_copy, ",");
    while (token != NULL) {
        while (*token == ' ') {
            token++;
        }
        if (*token != '\0') {
            cJSON_AddItemToArray(array, cJSON_CreateString(token));
        }
        token = strtok(NULL, ",");
    }
}

static void build_config_object(cJSON *root)
{
    const device_option_t *device_option = find_device_option(s_state.device_name);

    cJSON_AddStringToObject(root, "deviceId", device_option->device_id);
    cJSON_AddStringToObject(root, "deviceName", s_state.device_name);
    cJSON_AddStringToObject(root, "deviceAlias", device_option->device_alias);
    append_json_string_array(root, "sensors", s_state.sensors_csv);
}

static void build_hardware_object(cJSON *root)
{
    cJSON *hardware = cJSON_AddObjectToObject(root, "hardware");
    cJSON_AddStringToObject(hardware, "chipModel", s_state.chip_model);
    cJSON_AddStringToObject(hardware, "fwVersion", s_state.fw_version);
    cJSON_AddStringToObject(hardware, "target", s_state.target);
    cJSON_AddNumberToObject(hardware, "cores", s_state.chip_cores);
    cJSON_AddNumberToObject(hardware, "revision", s_state.chip_revision);
    cJSON_AddStringToObject(hardware, "mac", s_state.mac);
}

static void load_wifi_list_from_nvs(nvs_handle_t handle)
{
    char json[1024] = {0};
    size_t required = sizeof(json);
    if (nvs_get_str(handle, "wifi_list", json, &required) != ESP_OK || json[0] == '\0') {
        /* default: single entry from app_config.h */
        strlcpy(s_wifi_entries[0].ssid, APP_WIFI_SSID, sizeof(s_wifi_entries[0].ssid));
        strlcpy(s_wifi_entries[0].password, APP_WIFI_PASSWORD, sizeof(s_wifi_entries[0].password));
        s_wifi_count = 1;
        return;
    }

    cJSON *arr = cJSON_Parse(json);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        strlcpy(s_wifi_entries[0].ssid, APP_WIFI_SSID, sizeof(s_wifi_entries[0].ssid));
        strlcpy(s_wifi_entries[0].password, APP_WIFI_PASSWORD, sizeof(s_wifi_entries[0].password));
        s_wifi_count = 1;
        return;
    }

    s_wifi_count = 0;
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && i < WIFI_MAX_ENTRIES; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        cJSON *ssid = cJSON_GetObjectItemCaseSensitive(item, "ssid");
        cJSON *pw   = cJSON_GetObjectItemCaseSensitive(item, "password");
        if (cJSON_IsString(ssid) && ssid->valuestring[0] != '\0') {
            strlcpy(s_wifi_entries[s_wifi_count].ssid, ssid->valuestring, sizeof(s_wifi_entries[0].ssid));
            strlcpy(s_wifi_entries[s_wifi_count].password,
                    cJSON_IsString(pw) ? pw->valuestring : "",
                    sizeof(s_wifi_entries[0].password));
            s_wifi_count++;
        }
    }
    cJSON_Delete(arr);

    if (s_wifi_count == 0) {
        strlcpy(s_wifi_entries[0].ssid, APP_WIFI_SSID, sizeof(s_wifi_entries[0].ssid));
        strlcpy(s_wifi_entries[0].password, APP_WIFI_PASSWORD, sizeof(s_wifi_entries[0].password));
        s_wifi_count = 1;
    }
}

esp_err_t device_profile_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    set_default_strings();
    populate_hardware_info();

    /* Initialize NVS here so subsequent callers (network_service) can use it */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
    }

    nvs_handle_t handle;
    if (nvs_open(DEVICE_PROFILE_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        load_nvs_string(handle, "device_name", s_state.device_name, sizeof(s_state.device_name));
        load_nvs_string(handle, "sensors_csv", s_state.sensors_csv, sizeof(s_state.sensors_csv));
        load_wifi_list_from_nvs(handle);
        nvs_close(handle);
    } else {
        /* NVS open failed, use compile-time defaults */
        strlcpy(s_wifi_entries[0].ssid, APP_WIFI_SSID, sizeof(s_wifi_entries[0].ssid));
        strlcpy(s_wifi_entries[0].password, APP_WIFI_PASSWORD, sizeof(s_wifi_entries[0].password));
        s_wifi_count = 1;
    }

    return ESP_OK;
}

const char *device_profile_device_name(void)
{
    return s_state.device_name;
}

const char *device_profile_device_id(void)
{
    return find_device_option(s_state.device_name)->device_id;
}

const char *device_profile_device_alias(void)
{
    return find_device_option(s_state.device_name)->device_alias;
}

const char *device_profile_mqtt_topic(void)
{
    static char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", APP_MQTT_TOPIC_PREFIX, device_profile_device_id());
    return topic;
}

void device_profile_update_wifi(bool connected, const char *ssid, const char *ip, int disconnect_reason)
{
    profile_lock();
    s_state.wifi_connected = connected;
    s_state.wifi_disconnect_reason = disconnect_reason;
    if (ssid != NULL && ssid[0] != '\0') {
        snprintf(s_state.wifi_ssid, sizeof(s_state.wifi_ssid), "%s", ssid);
    } else if (!connected) {
        snprintf(s_state.wifi_ssid, sizeof(s_state.wifi_ssid), "--");
    }
    if (ip != NULL && ip[0] != '\0') {
        snprintf(s_state.wifi_ip, sizeof(s_state.wifi_ip), "%s", ip);
    } else if (!connected) {
        snprintf(s_state.wifi_ip, sizeof(s_state.wifi_ip), "0.0.0.0");
    }
    profile_unlock();
}

void device_profile_update_mqtt(bool connected)
{
    profile_lock();
    s_state.mqtt_connected = connected;
    profile_unlock();
}

void device_profile_update_dht11(bool ready, float temperature_c, float humidity_pct)
{
    profile_lock();
    s_state.dht11_ready = ready;
    s_state.dht11_temperature_c = temperature_c;
    s_state.dht11_humidity_pct = humidity_pct;
    profile_unlock();
}

void device_profile_update_publish(bool ready, float temperature_c, float humidity_pct, int rssi, const char *payload)
{
    profile_lock();
    s_state.publish.ready = ready;
    s_state.publish.temperature_c = temperature_c;
    s_state.publish.humidity_pct = humidity_pct;
    s_state.publish.rssi = rssi;
    if (payload != NULL && payload[0] != '\0') {
        snprintf(s_state.publish.payload, sizeof(s_state.publish.payload), "%s", payload);
    }
    profile_unlock();
}

void device_profile_build_config_json(char *buffer, size_t buffer_size)
{
    profile_lock();
    cJSON *root = cJSON_CreateObject();
    build_config_object(root);
    build_hardware_object(root);
    char *printed = cJSON_PrintUnformatted(root);
    snprintf(buffer, buffer_size, "%s", printed != NULL ? printed : "{}");
    cJSON_free(printed);
    cJSON_Delete(root);
    profile_unlock();
}

void device_profile_build_status_json(char *buffer, size_t buffer_size)
{
    profile_lock();
    cJSON *root = cJSON_CreateObject();
    build_config_object(root);
    build_hardware_object(root);

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddBoolToObject(wifi, "connected", s_state.wifi_connected);
    cJSON_AddStringToObject(wifi, "ssid", s_state.wifi_ssid);
    cJSON_AddStringToObject(wifi, "ip", s_state.wifi_ip);
    cJSON_AddNumberToObject(wifi, "disconnectReason", s_state.wifi_disconnect_reason);

    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddBoolToObject(mqtt, "connected", s_state.mqtt_connected);
    cJSON_AddStringToObject(mqtt, "broker", APP_MQTT_URI);
    cJSON_AddStringToObject(mqtt, "topic", device_profile_mqtt_topic());

    cJSON *sensor = cJSON_AddObjectToObject(root, "dht11");
    cJSON_AddBoolToObject(sensor, "ready", s_state.dht11_ready);
    cJSON_AddNumberToObject(sensor, "temperature", s_state.dht11_temperature_c);
    cJSON_AddNumberToObject(sensor, "humidity", s_state.dht11_humidity_pct);

    cJSON *publish = cJSON_AddObjectToObject(root, "publish");
    cJSON_AddBoolToObject(publish, "ready", s_state.publish.ready);
    cJSON_AddStringToObject(publish, "device", find_device_option(s_state.device_name)->device_id);
    cJSON_AddStringToObject(publish, "alias", find_device_option(s_state.device_name)->device_alias);
    cJSON_AddNumberToObject(publish, "temperature", s_state.publish.temperature_c);
    cJSON_AddNumberToObject(publish, "humidity", s_state.publish.humidity_pct);
    cJSON_AddNumberToObject(publish, "rssi", s_state.publish.rssi);

    char *printed = cJSON_PrintUnformatted(root);
    snprintf(buffer, buffer_size, "%s", printed != NULL ? printed : "{}");
    cJSON_free(printed);
    cJSON_Delete(root);
    profile_unlock();
}

void device_profile_build_options_json(char *buffer, size_t buffer_size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *device_names = cJSON_AddArrayToObject(root, "deviceNames");
    cJSON *sensor_types = cJSON_AddArrayToObject(root, "sensorTypes");

    for (size_t i = 0; i < sizeof(s_device_options) / sizeof(s_device_options[0]); i++) {
        cJSON_AddItemToArray(device_names, cJSON_CreateString(s_device_options[i].name));
    }
    for (size_t i = 0; i < sizeof(s_sensor_type_options) / sizeof(s_sensor_type_options[0]); i++) {
        cJSON_AddItemToArray(sensor_types, cJSON_CreateString(s_sensor_type_options[i]));
    }

    char *printed = cJSON_PrintUnformatted(root);
    snprintf(buffer, buffer_size, "%s", printed != NULL ? printed : "{}");
    cJSON_free(printed);
    cJSON_Delete(root);
}

int device_profile_get_wifi_count(void)
{
    return s_wifi_count;
}

bool device_profile_get_wifi_entry(int index, char *ssid, size_t ssid_size, char *password, size_t pw_size)
{
    if (index < 0 || index >= s_wifi_count) {
        return false;
    }
    strlcpy(ssid, s_wifi_entries[index].ssid, ssid_size);
    strlcpy(password, s_wifi_entries[index].password, pw_size);
    return true;
}

void device_profile_get_wifi_list_json(char *buffer, size_t buffer_size)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_wifi_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", s_wifi_entries[i].ssid);
        cJSON_AddStringToObject(item, "password", s_wifi_entries[i].password);
        cJSON_AddItemToArray(arr, item);
    }
    char *printed = cJSON_PrintUnformatted(arr);
    snprintf(buffer, buffer_size, "%s", printed != NULL ? printed : "[]");
    cJSON_free(printed);
    cJSON_Delete(arr);
}

esp_err_t device_profile_set_wifi_list_json(const char *json_text, char *message, size_t message_size)
{
    cJSON *arr = cJSON_Parse(json_text);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        snprintf(message, message_size, "invalid json array");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_entry_t new_entries[WIFI_MAX_ENTRIES];
    int new_count = 0;
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && i < WIFI_MAX_ENTRIES; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        cJSON *ssid = cJSON_GetObjectItemCaseSensitive(item, "ssid");
        cJSON *pw   = cJSON_GetObjectItemCaseSensitive(item, "password");
        if (cJSON_IsString(ssid) && ssid->valuestring[0] != '\0') {
            strlcpy(new_entries[new_count].ssid, ssid->valuestring, sizeof(new_entries[0].ssid));
            strlcpy(new_entries[new_count].password,
                    cJSON_IsString(pw) ? pw->valuestring : "",
                    sizeof(new_entries[0].password));
            new_count++;
        }
    }
    cJSON_Delete(arr);

    if (new_count == 0) {
        snprintf(message, message_size, "no valid entries");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(s_wifi_entries, new_entries, new_count * sizeof(wifi_entry_t));
    s_wifi_count = new_count;

    nvs_handle_t handle;
    if (nvs_open(DEVICE_PROFILE_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "wifi_list", json_text);
        nvs_commit(handle);
        nvs_close(handle);
    }

    snprintf(message, message_size, "wifi list saved (%d entries)", new_count);
    return ESP_OK;
}

esp_err_t device_profile_apply_config_json(const char *json_text, char *message, size_t message_size)
{
    cJSON *root = cJSON_Parse(json_text);
    if (root == NULL) {
        snprintf(message, message_size, "invalid json");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *device_name = cJSON_GetObjectItemCaseSensitive(root, "deviceName");
    cJSON *sensors = cJSON_GetObjectItemCaseSensitive(root, "sensors");

    profile_lock();
    if (cJSON_IsString(device_name) && device_name->valuestring[0] != '\0') {
        snprintf(s_state.device_name, sizeof(s_state.device_name), "%s", device_name->valuestring);
    }
    if (cJSON_IsArray(sensors)) {
        s_state.sensors_csv[0] = '\0';
        int count = cJSON_GetArraySize(sensors);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(sensors, i);
            if (!cJSON_IsString(item) || item->valuestring[0] == '\0') {
                continue;
            }
            if (s_state.sensors_csv[0] != '\0') {
                strlcat(s_state.sensors_csv, ",", sizeof(s_state.sensors_csv));
            }
            strlcat(s_state.sensors_csv, item->valuestring, sizeof(s_state.sensors_csv));
        }
        if (s_state.sensors_csv[0] == '\0') {
            snprintf(s_state.sensors_csv, sizeof(s_state.sensors_csv), "%s", DEFAULT_SENSOR_LIST);
        }
    }
    save_nvs_strings();
    profile_unlock();

    snprintf(message, message_size, "config saved");
    cJSON_Delete(root);
    ESP_LOGI(TAG, "config updated: name=%s sensors=%s", s_state.device_name, s_state.sensors_csv);
    return ESP_OK;
}
