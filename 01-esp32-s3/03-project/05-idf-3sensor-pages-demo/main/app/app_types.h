#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_BMP180,
    SENSOR_TYPE_BMP280,
    SENSOR_TYPE_BME280,
} sensor_type_t;

typedef struct {
    bool ready;
    float temperature_c;
    float pressure_pa;
    float pressure_hpa;
    float altitude_m;
} pressure_sample_t;

typedef struct {
    bool ready;
    float temperature_c;
} ds18b20_sample_t;

typedef struct {
    bool ready;
    float temperature_c;
    float humidity_pct;
} dht11_sample_t;

typedef struct {
    bool ready;
    float lux;
} bh1750_sample_t;

typedef struct {
    bool ready;
    int raw;
    float voltage_v;
    float moisture_pct;
} soil_moisture_sample_t;

typedef struct {
    bool command_received;
    bool active;
    uint32_t duration_seconds;
    uint32_t remaining_seconds;
    int64_t started_at_us;
    int64_t stop_at_us;
    char requested_by[32];
    char issued_at[40];
} pump_state_t;

typedef struct {
    bool valid;
    bool server_online;
    bool mqtt_online;
    bool http_online;
    bool public_url_available;
    float cpu_temperature_c;
    int64_t updated_at_us;
} gateway_status_t;

typedef struct {
    pressure_sample_t pressure;
    ds18b20_sample_t ds18b20;
    dht11_sample_t dht11;
    bh1750_sample_t bh1750;
    soil_moisture_sample_t soil_moisture;
    pump_state_t pump;
} app_samples_t;

#endif
