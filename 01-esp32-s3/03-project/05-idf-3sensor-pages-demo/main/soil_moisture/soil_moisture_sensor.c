#include "soil_moisture/soil_moisture_sensor.h"

#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"

#include "app/app_config.h"

static adc_oneshot_unit_handle_t s_adc_handle;
static bool s_soil_moisture_ready;

static float soil_moisture_pct_from_raw(int raw)
{
    float pct = ((float)(SOIL_MOISTURE_ADC_DRY_RAW - raw) * 100.0f) /
                (float)(SOIL_MOISTURE_ADC_DRY_RAW - SOIL_MOISTURE_ADC_WET_RAW);

    if (pct < 0.0f) {
        return 0.0f;
    }
    if (pct > 100.0f) {
        return 100.0f;
    }
    return pct;
}

esp_err_t soil_moisture_sensor_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = SOIL_MOISTURE_ADC_UNIT,
    };
    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = SOIL_MOISTURE_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle), APP_TAG, "adc unit init failed");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, SOIL_MOISTURE_ADC_CHANNEL, &channel_cfg),
                        APP_TAG, "soil moisture adc config failed");
    s_soil_moisture_ready = true;
    return ESP_OK;
}

bool soil_moisture_sensor_is_ready(void)
{
    return s_soil_moisture_ready;
}

esp_err_t soil_moisture_sensor_read(soil_moisture_sample_t *sample)
{
    int raw = 0;
    memset(sample, 0, sizeof(*sample));

    if (!s_soil_moisture_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc_handle, SOIL_MOISTURE_ADC_CHANNEL, &raw),
                        APP_TAG, "soil moisture adc read failed");

    sample->ready = true;
    sample->raw = raw;
    sample->voltage_v = (3.3f * (float)raw) / 4095.0f;
    sample->moisture_pct = soil_moisture_pct_from_raw(raw);
    return ESP_OK;
}
