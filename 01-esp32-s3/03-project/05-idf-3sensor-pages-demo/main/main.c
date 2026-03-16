#include <math.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app/app_config.h"
#include "app/app_types.h"
#include "bh1750/bh1750_sensor.h"
#include "board/board_support.h"
#include "dht11/dht11_sensor.h"
#include "ds18b20/ds18b20_sensor.h"
#include "i2c_bus/i2c_bus.h"
#include "network/network_service.h"
#include "oled_display/oled_display.h"
#include "page_button/page_button.h"
#include "pressure/pressure_sensor.h"
#include "soil_moisture/soil_moisture_sensor.h"
#include "ui/ui_pages.h"

typedef struct {
    pressure_sample_t last_pressure;
    pressure_sample_t filtered_pressure;
    ds18b20_sample_t last_ds18b20;
    ds18b20_sample_t filtered_ds18b20;
    dht11_sample_t filtered_dht11;
    bh1750_sample_t last_bh1750;
    bh1750_sample_t filtered_bh1750;
    soil_moisture_sample_t last_soil_moisture;
    soil_moisture_sample_t filtered_soil_moisture;
} sample_cache_t;

static int s_page_index;
static int s_seq;

static float low_pass_filter(float current, float input, float alpha)
{
    return current + alpha * (input - current);
}

static void apply_pressure_filter(sample_cache_t *cache, pressure_sample_t *sample)
{
    if (!sample->ready) {
        return;
    }

    if (!cache->filtered_pressure.ready) {
        cache->filtered_pressure = *sample;
    } else {
        cache->filtered_pressure.temperature_c =
            low_pass_filter(cache->filtered_pressure.temperature_c, sample->temperature_c, PRESSURE_FILTER_ALPHA);
        cache->filtered_pressure.pressure_pa =
            low_pass_filter(cache->filtered_pressure.pressure_pa, sample->pressure_pa, PRESSURE_FILTER_ALPHA);
        cache->filtered_pressure.pressure_hpa = cache->filtered_pressure.pressure_pa / 100.0f;
        cache->filtered_pressure.altitude_m =
            low_pass_filter(cache->filtered_pressure.altitude_m, sample->altitude_m, PRESSURE_FILTER_ALPHA);
    }

    cache->filtered_pressure.ready = true;
    *sample = cache->filtered_pressure;
}

static void apply_ds18b20_filter(sample_cache_t *cache, ds18b20_sample_t *sample)
{
    if (!sample->ready) {
        return;
    }

    if (!cache->filtered_ds18b20.ready) {
        cache->filtered_ds18b20 = *sample;
    } else {
        cache->filtered_ds18b20.temperature_c =
            low_pass_filter(cache->filtered_ds18b20.temperature_c, sample->temperature_c, DS18B20_FILTER_ALPHA);
    }

    cache->filtered_ds18b20.ready = true;
    *sample = cache->filtered_ds18b20;
}

static void apply_dht11_filter(sample_cache_t *cache, dht11_sample_t *sample)
{
    if (!sample->ready) {
        return;
    }

    if (!cache->filtered_dht11.ready) {
        cache->filtered_dht11 = *sample;
    } else {
        cache->filtered_dht11.temperature_c =
            low_pass_filter(cache->filtered_dht11.temperature_c, sample->temperature_c, DHT11_FILTER_ALPHA);
        cache->filtered_dht11.humidity_pct =
            low_pass_filter(cache->filtered_dht11.humidity_pct, sample->humidity_pct, DHT11_FILTER_ALPHA);
    }

    cache->filtered_dht11.ready = true;
    *sample = cache->filtered_dht11;
}

static void apply_bh1750_filter(sample_cache_t *cache, bh1750_sample_t *sample)
{
    if (!sample->ready) {
        return;
    }

    if (!cache->filtered_bh1750.ready) {
        cache->filtered_bh1750 = *sample;
    } else {
        cache->filtered_bh1750.lux =
            low_pass_filter(cache->filtered_bh1750.lux, sample->lux, BH1750_FILTER_ALPHA);
    }

    cache->filtered_bh1750.ready = true;
    *sample = cache->filtered_bh1750;
}

static void apply_soil_moisture_filter(sample_cache_t *cache, soil_moisture_sample_t *sample)
{
    if (!sample->ready) {
        return;
    }

    if (!cache->filtered_soil_moisture.ready) {
        cache->filtered_soil_moisture = *sample;
    } else {
        cache->filtered_soil_moisture.moisture_pct =
            low_pass_filter(cache->filtered_soil_moisture.moisture_pct, sample->moisture_pct, SOIL_MOISTURE_FILTER_ALPHA);
        cache->filtered_soil_moisture.voltage_v =
            low_pass_filter(cache->filtered_soil_moisture.voltage_v, sample->voltage_v, SOIL_MOISTURE_FILTER_ALPHA);
        cache->filtered_soil_moisture.raw = (int)lroundf(
            low_pass_filter((float)cache->filtered_soil_moisture.raw, (float)sample->raw, SOIL_MOISTURE_FILTER_ALPHA)
        );
    }

    cache->filtered_soil_moisture.ready = true;
    *sample = cache->filtered_soil_moisture;
}

static void log_samples(const app_samples_t *samples)
{
    s_seq++;
    ESP_LOGI(
        APP_TAG,
        "{\"seq\":%d,\"press_ready\":%d,\"press_chip\":\"%s\",\"press_hpa\":%.2f,\"press_temp_c\":%.2f,"
        "\"ds18_ready\":%d,\"ds18_temp_c\":%.2f,\"dht11_ready\":%d,\"dht11_temp_c\":%.1f,\"dht11_humi\":%.1f,"
        "\"bh1750_ready\":%d,\"bh1750_lux\":%.2f,\"soil_ready\":%d,\"soil_pct\":%.1f,\"soil_raw\":%d}",
        s_seq,
        samples->pressure.ready ? 1 : 0,
        pressure_sensor_label(pressure_sensor_type()),
        samples->pressure.pressure_hpa,
        samples->pressure.temperature_c,
        samples->ds18b20.ready ? 1 : 0,
        samples->ds18b20.temperature_c,
        samples->dht11.ready ? 1 : 0,
        samples->dht11.temperature_c,
        samples->dht11.humidity_pct,
        samples->bh1750.ready ? 1 : 0,
        samples->bh1750.lux,
        samples->soil_moisture.ready ? 1 : 0,
        samples->soil_moisture.moisture_pct,
        samples->soil_moisture.raw
    );
}

static void render_active_page(const app_samples_t *samples)
{
    char wifi_text[12];
    char server_text[12];

    network_service_tick();
    network_service_format_status(wifi_text, sizeof(wifi_text), server_text, sizeof(server_text));
    ui_render_current_page(s_page_index, samples, wifi_text, server_text);
}

void app_main(void)
{
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_LOGI(APP_TAG, "Start 5 sensor pages demo");
    ESP_LOGI(APP_TAG, "I2C: SDA=GPIO%d SCL=GPIO%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    ESP_LOGI(APP_TAG, "DS18B20 on GPIO%d, DHT11 on GPIO%d, soil moisture on GPIO%d, page button on GPIO%d",
             DS18B20_GPIO, DHT11_GPIO, SOIL_MOISTURE_GPIO, PAGE_BUTTON_GPIO);

    board_rgb_off();
    page_button_init();

    if (network_service_configured()) {
        ESP_LOGI(APP_TAG, "Starting WiFi STA and MQTT uploader");
        network_service_start();
    } else {
        ESP_LOGW(APP_TAG, "WiFi credentials are not configured, MQTT upload disabled");
    }

    ESP_ERROR_CHECK(i2c_bus_init());

    if (oled_display_init() == ESP_OK) {
        ESP_LOGI(APP_TAG, "OLED detected and initialized");
        ui_render_boot_screen();
    } else {
        ESP_LOGW(APP_TAG, "OLED not found at 0x3C/0x3D");
    }

    if (pressure_sensor_init() == ESP_OK) {
        ESP_LOGI(APP_TAG, "Pressure sensor detected: %s", pressure_sensor_label(pressure_sensor_type()));
    } else {
        ESP_LOGW(APP_TAG, "Pressure sensor not found");
    }

    if (bh1750_sensor_init() == ESP_OK) {
        ESP_LOGI(APP_TAG, "BH1750 detected at 0x%02X", bh1750_sensor_address());
    } else {
        ESP_LOGW(APP_TAG, "BH1750 not found at 0x23/0x5C");
    }

    ds18b20_sensor_init(DS18B20_GPIO);
    dht11_sensor_init(DHT11_GPIO);

    if (soil_moisture_sensor_init() == ESP_OK) {
        ESP_LOGI(APP_TAG, "Soil moisture ADC ready on GPIO%d", SOIL_MOISTURE_GPIO);
    } else {
        ESP_LOGW(APP_TAG, "Soil moisture ADC init failed");
    }

    sample_cache_t cache = {0};
    app_samples_t samples = {0};

    while (1) {
        samples = (app_samples_t){0};

        if (pressure_sensor_type() != SENSOR_TYPE_NONE) {
            esp_err_t ret = pressure_sensor_read(&samples.pressure);
            if (ret != ESP_OK) {
                ESP_LOGW(APP_TAG, "Pressure read failed: %s", esp_err_to_name(ret));
                if (cache.filtered_pressure.ready) {
                    samples.pressure = cache.filtered_pressure;
                } else if (cache.last_pressure.ready) {
                    samples.pressure = cache.last_pressure;
                }
            } else {
                cache.last_pressure = samples.pressure;
                apply_pressure_filter(&cache, &samples.pressure);
            }
        }

        esp_err_t ds_ret = ds18b20_sensor_read(&samples.ds18b20);
        if (ds_ret != ESP_OK) {
            ESP_LOGW(APP_TAG, "DS18B20 read failed: %s", esp_err_to_name(ds_ret));
            if (cache.filtered_ds18b20.ready) {
                samples.ds18b20 = cache.filtered_ds18b20;
            } else if (cache.last_ds18b20.ready) {
                samples.ds18b20 = cache.last_ds18b20;
            }
        } else {
            cache.last_ds18b20 = samples.ds18b20;
            apply_ds18b20_filter(&cache, &samples.ds18b20);
        }

        esp_err_t dht_ret = dht11_sensor_read(&samples.dht11);
        if (dht_ret != ESP_OK) {
            ESP_LOGW(APP_TAG, "DHT11 read failed: %s", esp_err_to_name(dht_ret));
            if (cache.filtered_dht11.ready) {
                samples.dht11 = cache.filtered_dht11;
            }
        } else {
            apply_dht11_filter(&cache, &samples.dht11);
        }

        if (bh1750_sensor_is_ready()) {
            esp_err_t bh_ret = bh1750_sensor_read(&samples.bh1750);
            if (bh_ret != ESP_OK) {
                ESP_LOGW(APP_TAG, "BH1750 read failed: %s", esp_err_to_name(bh_ret));
                if (cache.filtered_bh1750.ready) {
                    samples.bh1750 = cache.filtered_bh1750;
                } else if (cache.last_bh1750.ready) {
                    samples.bh1750 = cache.last_bh1750;
                }
            } else {
                cache.last_bh1750 = samples.bh1750;
                apply_bh1750_filter(&cache, &samples.bh1750);
            }
        }

        if (soil_moisture_sensor_is_ready()) {
            esp_err_t soil_ret = soil_moisture_sensor_read(&samples.soil_moisture);
            if (soil_ret != ESP_OK) {
                ESP_LOGW(APP_TAG, "Soil moisture read failed: %s", esp_err_to_name(soil_ret));
                if (cache.filtered_soil_moisture.ready) {
                    samples.soil_moisture = cache.filtered_soil_moisture;
                } else if (cache.last_soil_moisture.ready) {
                    samples.soil_moisture = cache.last_soil_moisture;
                }
            } else {
                cache.last_soil_moisture = samples.soil_moisture;
                apply_soil_moisture_filter(&cache, &samples.soil_moisture);
            }
        }

        log_samples(&samples);
        network_service_publish_samples(&samples);
        render_active_page(&samples);

        for (int elapsed_ms = 0; elapsed_ms < PAGE_REFRESH_MS; elapsed_ms += UI_POLL_MS) {
            if (page_button_was_pressed()) {
                s_page_index = (s_page_index + 1) % ui_pages_count();
                ESP_LOGI(APP_TAG, "Page changed to %d", s_page_index + 1);
                render_active_page(&samples);
            }

            vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
        }
    }
}
