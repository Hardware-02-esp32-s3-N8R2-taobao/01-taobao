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

#define DEFAULT_DEVICE_NAME   "庭院1号"
#define DEFAULT_DEVICE_ALIAS  "庭院1号设备"
#define DEFAULT_SENSOR_LIST   "dht11"

typedef struct {
    char device_name[32];
    char device_alias[48];
    char device_source[48];
    char sensors_csv[96];
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

static const char *s_device_name_options[] = {
    "庭院1号",
    "卧室1号",
    "书房1号",
    "办公室1号",
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
    snprintf(s_state.device_alias, sizeof(s_state.device_alias), "%s", DEFAULT_DEVICE_ALIAS);
    snprintf(s_state.device_source, sizeof(s_state.device_source), "%s", APP_DEVICE_SOURCE);
    snprintf(s_state.sensors_csv, sizeof(s_state.sensors_csv), "%s", DEFAULT_SENSOR_LIST);
    snprintf(s_state.wifi_ssid, sizeof(s_state.wifi_ssid), "%s", APP_WIFI_SSID);
    snprintf(s_state.wifi_ip, sizeof(s_state.wifi_ip), "0.0.0.0");
    snprintf(s_state.publish.ip, sizeof(s_state.publish.ip), "0.0.0.0");
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
        ESP_LOGW(TAG, "open nvs failed");
        return;
    }

    nvs_set_str(handle, "device_name", s_state.device_name);
    nvs_set_str(handle, "device_alias", s_state.device_alias);
    nvs_set_str(handle, "device_source", s_state.device_source);
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
    case CHIP_ESP32P4:
        return "ESP32-P4";
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
    cJSON_AddStringToObject(root, "deviceId", APP_DEVICE_ID);
    cJSON_AddStringToObject(root, "deviceName", s_state.device_name);
    cJSON_AddStringToObject(root, "deviceAlias", s_state.device_alias);
    cJSON_AddStringToObject(root, "deviceSource", s_state.device_source);
    append_json_string_array(root, "sensors", s_state.sensors_csv);
}

static void build_hardware_object(cJSON *root)
{
    cJSON *hardware = cJSON_AddObjectToObject(root, "hardware");
    cJSON_AddStringToObject(hardware, "chipModel", s_state.chip_model);
    cJSON_AddStringToObject(hardware, "target", s_state.target);
    cJSON_AddNumberToObject(hardware, "cores", s_state.chip_cores);
    cJSON_AddNumberToObject(hardware, "revision", s_state.chip_revision);
    cJSON_AddStringToObject(hardware, "mac", s_state.mac);
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

    nvs_handle_t handle;
    if (nvs_open(DEVICE_PROFILE_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        load_nvs_string(handle, "device_name", s_state.device_name, sizeof(s_state.device_name));
        load_nvs_string(handle, "device_alias", s_state.device_alias, sizeof(s_state.device_alias));
        load_nvs_string(handle, "device_source", s_state.device_source, sizeof(s_state.device_source));
        load_nvs_string(handle, "sensors_csv", s_state.sensors_csv, sizeof(s_state.sensors_csv));
        nvs_close(handle);
    }

    ESP_LOGI(
        TAG,
        "config loaded: deviceName=%s alias=%s sensors=%s chip=%s",
        s_state.device_name,
        s_state.device_alias,
        s_state.sensors_csv,
        s_state.chip_model
    );
    return ESP_OK;
}

const char *device_profile_device_id(void)
{
    return APP_DEVICE_ID;
}

const char *device_profile_device_name(void)
{
    return s_state.device_name;
}

const char *device_profile_device_alias(void)
{
    return s_state.device_alias;
}

const char *device_profile_device_source(void)
{
    return s_state.device_source;
}

void device_profile_update_wifi(bool connected, const char *ip, int disconnect_reason)
{
    profile_lock();
    s_state.wifi_connected = connected;
    s_state.wifi_disconnect_reason = disconnect_reason;
    if (ip != NULL && ip[0] != '\0') {
        snprintf(s_state.wifi_ip, sizeof(s_state.wifi_ip), "%s", ip);
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

void device_profile_update_publish(bool ready, float temperature_c, float humidity_pct, int rssi, const char *ip, const char *payload)
{
    profile_lock();
    s_state.publish.ready = ready;
    s_state.publish.temperature_c = temperature_c;
    s_state.publish.humidity_pct = humidity_pct;
    s_state.publish.rssi = rssi;
    if (ip != NULL && ip[0] != '\0') {
        snprintf(s_state.publish.ip, sizeof(s_state.publish.ip), "%s", ip);
    }
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
    cJSON_AddStringToObject(mqtt, "topic", APP_MQTT_TOPIC_TELEMETRY);

    cJSON *sensor = cJSON_AddObjectToObject(root, "dht11");
    cJSON_AddBoolToObject(sensor, "ready", s_state.dht11_ready);
    cJSON_AddNumberToObject(sensor, "temperature", s_state.dht11_temperature_c);
    cJSON_AddNumberToObject(sensor, "humidity", s_state.dht11_humidity_pct);

    cJSON *publish = cJSON_AddObjectToObject(root, "publish");
    cJSON_AddBoolToObject(publish, "ready", s_state.publish.ready);
    cJSON_AddNumberToObject(publish, "temperature", s_state.publish.temperature_c);
    cJSON_AddNumberToObject(publish, "humidity", s_state.publish.humidity_pct);
    cJSON_AddNumberToObject(publish, "rssi", s_state.publish.rssi);
    cJSON_AddStringToObject(publish, "ip", s_state.publish.ip);
    cJSON_AddStringToObject(publish, "payload", s_state.publish.payload);

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

    for (size_t i = 0; i < sizeof(s_device_name_options) / sizeof(s_device_name_options[0]); i++) {
        cJSON_AddItemToArray(device_names, cJSON_CreateString(s_device_name_options[i]));
    }
    for (size_t i = 0; i < sizeof(s_sensor_type_options) / sizeof(s_sensor_type_options[0]); i++) {
        cJSON_AddItemToArray(sensor_types, cJSON_CreateString(s_sensor_type_options[i]));
    }

    char *printed = cJSON_PrintUnformatted(root);
    snprintf(buffer, buffer_size, "%s", printed != NULL ? printed : "{}");
    cJSON_free(printed);
    cJSON_Delete(root);
}

esp_err_t device_profile_apply_config_json(const char *json_text, char *message, size_t message_size)
{
    cJSON *root = cJSON_Parse(json_text);
    if (root == NULL) {
        snprintf(message, message_size, "invalid json");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *device_name = cJSON_GetObjectItemCaseSensitive(root, "deviceName");
    cJSON *device_alias = cJSON_GetObjectItemCaseSensitive(root, "deviceAlias");
    cJSON *device_source = cJSON_GetObjectItemCaseSensitive(root, "deviceSource");
    cJSON *sensors = cJSON_GetObjectItemCaseSensitive(root, "sensors");

    profile_lock();
    if (cJSON_IsString(device_name) && device_name->valuestring[0] != '\0') {
        snprintf(s_state.device_name, sizeof(s_state.device_name), "%s", device_name->valuestring);
    }
    if (cJSON_IsString(device_alias) && device_alias->valuestring[0] != '\0') {
        snprintf(s_state.device_alias, sizeof(s_state.device_alias), "%s", device_alias->valuestring);
    }
    if (cJSON_IsString(device_source) && device_source->valuestring[0] != '\0') {
        snprintf(s_state.device_source, sizeof(s_state.device_source), "%s", device_source->valuestring);
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
    ESP_LOGI(TAG, "config updated: name=%s alias=%s sensors=%s", s_state.device_name, s_state.device_alias, s_state.sensors_csv);
    return ESP_OK;
}
