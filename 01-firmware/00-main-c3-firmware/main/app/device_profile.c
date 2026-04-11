#include "device_profile.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/i2c.h"

#include "app_config.h"

#define TAG "device_profile"
#define DEVICE_PROFILE_NAMESPACE "device_cfg"
#define WIFI_MAX_ENTRIES 8
#define HW_DETECT_I2C_PORT I2C_NUM_0
#define HW_DETECT_SDA_GPIO GPIO_NUM_5
#define HW_DETECT_SCL_GPIO GPIO_NUM_6
#define HW_DETECT_OLED_ADDR_PRIMARY 0x3C
#define HW_DETECT_OLED_ADDR_SECONDARY 0x3D

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

#define DEFAULT_DEVICE_NAME   "探索者1号"
#define DEFAULT_SENSOR_LIST   ""

typedef struct {
    char device_name[32];
    char sensors_csv[96];
    char sensors_json[1536];
    int sensors_ready_count;
    int sensors_total_count;
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
    device_hw_variant_t hardware_variant;
    char hardware_variant_name[24];
    bool low_power_enabled;
    uint32_t low_power_interval_sec;
} device_profile_state_t;

static device_profile_state_t s_state;
static SemaphoreHandle_t s_lock;

static const device_option_t s_device_options[] = {
    { "探索者1号", "explorer-01", "探索者 1 号" },
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
    "shtc3",
    "soil_moisture",
    "battery",
    "max17043",
    "ina226",
};

static const char *normalize_sensor_type(const char *sensor_type)
{
    if (sensor_type == NULL || sensor_type[0] == '\0') {
        return sensor_type;
    }
    if (strcmp(sensor_type, "bmp280") == 0 || strcmp(sensor_type, "bme280") == 0) {
        return "bmp180";
    }
    return sensor_type;
}

static const char *current_app_firmware_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc != NULL && app_desc->version[0] != '\0') {
        return app_desc->version;
    }
    return APP_FIRMWARE_VERSION;
}

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
    snprintf(s_state.sensors_json, sizeof(s_state.sensors_json), "%s", "{}");
    snprintf(s_state.fw_version, sizeof(s_state.fw_version), "%s", current_app_firmware_version());
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
    nvs_set_u8(handle, "lp_enabled", s_state.low_power_enabled ? 1 : 0);
    nvs_set_u32(handle, "lp_interval", s_state.low_power_interval_sec);
    nvs_commit(handle);
    nvs_close(handle);
}

static void apply_low_power_state(bool enabled, uint32_t interval_sec, bool persist)
{
    profile_lock();
    s_state.low_power_enabled = enabled;
    s_state.low_power_interval_sec = interval_sec >= 10U ? interval_sec : 300U;
    if (persist) {
        save_nvs_strings();
    }
    profile_unlock();
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

static esp_err_t i2c_probe_addr(i2c_port_t port, uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static device_hw_variant_t detect_hardware_variant(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = HW_DETECT_SDA_GPIO,
        .scl_io_num = HW_DETECT_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };

    if (i2c_param_config(HW_DETECT_I2C_PORT, &cfg) != ESP_OK) {
        return DEVICE_HW_VARIANT_SUPERMINI;
    }
    if (i2c_driver_install(HW_DETECT_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
        return DEVICE_HW_VARIANT_SUPERMINI;
    }

    esp_err_t primary = i2c_probe_addr(HW_DETECT_I2C_PORT, HW_DETECT_OLED_ADDR_PRIMARY);
    esp_err_t secondary = i2c_probe_addr(HW_DETECT_I2C_PORT, HW_DETECT_OLED_ADDR_SECONDARY);
    i2c_driver_delete(HW_DETECT_I2C_PORT);

    if (primary == ESP_OK || secondary == ESP_OK) {
        return DEVICE_HW_VARIANT_OLED_SCREEN;
    }
    return DEVICE_HW_VARIANT_SUPERMINI;
}

static const char *hardware_variant_to_text(device_hw_variant_t variant)
{
    switch (variant) {
    case DEVICE_HW_VARIANT_OLED_SCREEN:
        return "oled-screen";
    case DEVICE_HW_VARIANT_SUPERMINI:
        return "supermini";
    default:
        return "unknown";
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
    s_state.hardware_variant = detect_hardware_variant();
    snprintf(
        s_state.hardware_variant_name,
        sizeof(s_state.hardware_variant_name),
        "%s",
        hardware_variant_to_text(s_state.hardware_variant)
    );
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
            cJSON_AddItemToArray(array, cJSON_CreateString(normalize_sensor_type(token)));
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

static bool csv_contains_sensor(const char *csv, const char *sensor_type)
{
    if (csv == NULL || sensor_type == NULL || sensor_type[0] == '\0') {
        return false;
    }

    char local_copy[96];
    snprintf(local_copy, sizeof(local_copy), "%s", csv);

    char *token = strtok(local_copy, ",");
    while (token != NULL) {
        while (*token == ' ') {
            token++;
        }
        if (strcmp(normalize_sensor_type(token), normalize_sensor_type(sensor_type)) == 0) {
            return true;
        }
        token = strtok(NULL, ",");
    }
    return false;
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
    cJSON_AddStringToObject(hardware, "boardVariant", s_state.hardware_variant_name);
}

static void build_low_power_object(cJSON *root)
{
    cJSON *low_power = cJSON_AddObjectToObject(root, "lowPower");
    cJSON_AddBoolToObject(low_power, "enabled", s_state.low_power_enabled);
    cJSON_AddNumberToObject(low_power, "intervalSec", (double)s_state.low_power_interval_sec);
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
    s_state.low_power_enabled = false;
    s_state.low_power_interval_sec = 300;

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
        uint8_t low_power_enabled = 0;
        uint32_t low_power_interval = 300;
        nvs_get_u8(handle, "lp_enabled", &low_power_enabled);
        nvs_get_u32(handle, "lp_interval", &low_power_interval);
        s_state.low_power_enabled = (low_power_enabled != 0);
        s_state.low_power_interval_sec = (low_power_interval >= 10U) ? low_power_interval : 300U;
        if (csv_contains_sensor(s_state.sensors_csv, "bmp180") && strstr(s_state.sensors_csv, "bmp180") == NULL) {
            char migrated[96] = {0};
            char local_copy[96] = {0};
            snprintf(local_copy, sizeof(local_copy), "%s", s_state.sensors_csv);
            char *token = strtok(local_copy, ",");
            while (token != NULL) {
                while (*token == ' ') {
                    token++;
                }
                if (*token != '\0') {
                    if (migrated[0] != '\0') {
                        strlcat(migrated, ",", sizeof(migrated));
                    }
                    strlcat(migrated, normalize_sensor_type(token), sizeof(migrated));
                }
                token = strtok(NULL, ",");
            }
            if (migrated[0] != '\0') {
                snprintf(s_state.sensors_csv, sizeof(s_state.sensors_csv), "%s", migrated);
            }
            save_nvs_strings();
        }
        load_wifi_list_from_nvs(handle);
        nvs_close(handle);
    } else {
        /* NVS open failed, use compile-time defaults */
        strlcpy(s_wifi_entries[0].ssid, APP_WIFI_SSID, sizeof(s_wifi_entries[0].ssid));
        strlcpy(s_wifi_entries[0].password, APP_WIFI_PASSWORD, sizeof(s_wifi_entries[0].password));
        s_wifi_count = 1;
    }

    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
        apply_low_power_state(false, s_state.low_power_interval_sec, true);
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

const char *device_profile_firmware_version(void)
{
    return s_state.fw_version[0] != '\0' ? s_state.fw_version : current_app_firmware_version();
}

const char *device_profile_mqtt_topic(void)
{
    static char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", APP_MQTT_TOPIC_PREFIX, device_profile_device_id());
    return topic;
}

device_hw_variant_t device_profile_hardware_variant(void)
{
    return s_state.hardware_variant;
}

const char *device_profile_hardware_variant_name(void)
{
    return s_state.hardware_variant_name;
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

void device_profile_update_sensor_snapshot(const char *json_text, int ready_count, int total_count)
{
    profile_lock();
    snprintf(
        s_state.sensors_json,
        sizeof(s_state.sensors_json),
        "%s",
        (json_text != NULL && json_text[0] != '\0') ? json_text : "{}"
    );
    s_state.sensors_ready_count = ready_count;
    s_state.sensors_total_count = total_count;
    profile_unlock();
}

void device_profile_update_publish(bool ready, int rssi, const char *payload)
{
    profile_lock();
    s_state.publish.ready = ready;
    s_state.publish.temperature_c = s_state.dht11_temperature_c;
    s_state.publish.humidity_pct = s_state.dht11_humidity_pct;
    s_state.publish.rssi = rssi;
    if (payload != NULL && payload[0] != '\0') {
        snprintf(s_state.publish.payload, sizeof(s_state.publish.payload), "%s", payload);
    }
    profile_unlock();
}

bool device_profile_has_sensor(const char *sensor_type)
{
    bool enabled = false;
    profile_lock();
    enabled = csv_contains_sensor(s_state.sensors_csv, sensor_type);
    profile_unlock();
    return enabled;
}

void device_profile_copy_sensors_csv(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    profile_lock();
    snprintf(buffer, buffer_size, "%s", s_state.sensors_csv);
    profile_unlock();
}

void device_profile_build_config_json(char *buffer, size_t buffer_size)
{
    profile_lock();
    cJSON *root = cJSON_CreateObject();
    build_config_object(root);
    build_hardware_object(root);
    build_low_power_object(root);
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
    build_low_power_object(root);

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddBoolToObject(wifi, "connected", s_state.wifi_connected);
    cJSON_AddStringToObject(wifi, "ssid", s_state.wifi_ssid);
    cJSON_AddStringToObject(wifi, "ip", s_state.wifi_ip);
    cJSON_AddNumberToObject(wifi, "disconnectReason", s_state.wifi_disconnect_reason);

    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddBoolToObject(mqtt, "connected", s_state.mqtt_connected);
    cJSON_AddStringToObject(mqtt, "broker", APP_MQTT_URI);
    cJSON_AddStringToObject(mqtt, "topic", device_profile_mqtt_topic());

    cJSON_AddNumberToObject(root, "sensorReadyCount", s_state.sensors_ready_count);
    cJSON_AddNumberToObject(root, "sensorTotalCount", s_state.sensors_total_count);

    cJSON *sensors_data = cJSON_Parse(s_state.sensors_json);
    if (!cJSON_IsObject(sensors_data)) {
        cJSON_Delete(sensors_data);
        sensors_data = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(root, "sensorsData", sensors_data);

    cJSON *publish = cJSON_AddObjectToObject(root, "publish");
    cJSON_AddBoolToObject(publish, "ready", s_state.publish.ready);
    cJSON_AddStringToObject(publish, "device", find_device_option(s_state.device_name)->device_id);
    cJSON_AddStringToObject(publish, "alias", find_device_option(s_state.device_name)->device_alias);
    cJSON_AddNumberToObject(publish, "temperature", s_state.publish.temperature_c);
    cJSON_AddNumberToObject(publish, "humidity", s_state.publish.humidity_pct);
    cJSON_AddNumberToObject(publish, "rssi", s_state.publish.rssi);
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
    cJSON *low_power = cJSON_AddObjectToObject(root, "lowPower");

    for (size_t i = 0; i < sizeof(s_device_options) / sizeof(s_device_options[0]); i++) {
        cJSON_AddItemToArray(device_names, cJSON_CreateString(s_device_options[i].name));
    }
    for (size_t i = 0; i < sizeof(s_sensor_type_options) / sizeof(s_sensor_type_options[0]); i++) {
        cJSON_AddItemToArray(sensor_types, cJSON_CreateString(s_sensor_type_options[i]));
    }
    cJSON_AddNumberToObject(low_power, "minIntervalSec", 10);
    cJSON_AddNumberToObject(low_power, "maxIntervalSec", 86400);

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
            strlcat(s_state.sensors_csv, normalize_sensor_type(item->valuestring), sizeof(s_state.sensors_csv));
        }
    }
    save_nvs_strings();
    profile_unlock();

    snprintf(message, message_size, "config saved");
    cJSON_Delete(root);
    ESP_LOGI(TAG, "config updated: name=%s sensors=%s", s_state.device_name, s_state.sensors_csv);
    return ESP_OK;
}

bool device_profile_low_power_enabled(void)
{
    bool enabled = false;
    profile_lock();
    enabled = s_state.low_power_enabled;
    profile_unlock();
    return enabled;
}

uint32_t device_profile_low_power_interval_sec(void)
{
    uint32_t interval = 300;
    profile_lock();
    interval = s_state.low_power_interval_sec;
    profile_unlock();
    return interval;
}

void device_profile_set_low_power_state(bool enabled, uint32_t interval_sec)
{
    apply_low_power_state(enabled, interval_sec, true);
}

void device_profile_build_low_power_json(char *buffer, size_t buffer_size)
{
    profile_lock();
    cJSON *root = cJSON_CreateObject();
    build_low_power_object(root);
    char *printed = cJSON_PrintUnformatted(root);
    snprintf(buffer, buffer_size, "%s", printed != NULL ? printed : "{}");
    cJSON_free(printed);
    cJSON_Delete(root);
    profile_unlock();
}

esp_err_t device_profile_set_low_power_json(const char *json_text, char *message, size_t message_size)
{
    cJSON *root = cJSON_Parse(json_text);
    if (root == NULL) {
        snprintf(message, message_size, "invalid json");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    cJSON *interval_sec = cJSON_GetObjectItemCaseSensitive(root, "intervalSec");

    if (!cJSON_IsBool(enabled) && !cJSON_IsNumber(enabled)) {
        cJSON_Delete(root);
        snprintf(message, message_size, "enabled is required");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t interval = 300U;
    if (cJSON_IsNumber(interval_sec)) {
        interval = (uint32_t)interval_sec->valuedouble;
    } else {
        profile_lock();
        interval = s_state.low_power_interval_sec;
        profile_unlock();
    }
    if (interval < 10U) {
        cJSON_Delete(root);
        snprintf(message, message_size, "intervalSec must be >= 10");
        return ESP_ERR_INVALID_ARG;
    }

    apply_low_power_state(
        cJSON_IsTrue(enabled) || (cJSON_IsNumber(enabled) && enabled->valuedouble != 0),
        interval,
        true
    );

    snprintf(
        message,
        message_size,
        "low power %s, interval=%" PRIu32 "s",
        s_state.low_power_enabled ? "enabled" : "disabled",
        s_state.low_power_interval_sec
    );
    cJSON_Delete(root);
    return ESP_OK;
}
