#include "rain/rain_sensor.h"

#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"

#include "adc_shared/adc_shared.h"
#include "app/app_config.h"

static adc_oneshot_unit_handle_t s_adc_handle;
static bool s_rain_sensor_ready;

static float rain_level_pct_from_raw(int raw)
{
    float pct = ((float)(RAIN_SENSOR_ADC_DRY_RAW - raw) * 100.0f) /
                (float)(RAIN_SENSOR_ADC_DRY_RAW - RAIN_SENSOR_ADC_WET_RAW);

    if (pct < 0.0f) {
        return 0.0f;
    }
    if (pct > 100.0f) {
        return 100.0f;
    }
    return pct;
}

esp_err_t rain_sensor_init(void)
{
    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = RAIN_SENSOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(adc_shared_get_handle(RAIN_SENSOR_ADC_UNIT, &s_adc_handle), APP_TAG, "rain adc unit init failed");
    ESP_RETURN_ON_ERROR(
        adc_oneshot_config_channel(s_adc_handle, RAIN_SENSOR_ADC_CHANNEL, &channel_cfg),
        APP_TAG,
        "rain adc config failed"
    );
    s_rain_sensor_ready = true;
    return ESP_OK;
}

bool rain_sensor_is_ready(void)
{
    return s_rain_sensor_ready;
}

esp_err_t rain_sensor_read(rain_sensor_sample_t *sample)
{
    int raw = 0;
    memset(sample, 0, sizeof(*sample));

    if (!s_rain_sensor_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc_handle, RAIN_SENSOR_ADC_CHANNEL, &raw), APP_TAG, "rain adc read failed");

    sample->ready = true;
    sample->raw = raw;
    sample->voltage_v = (3.3f * (float)raw) / 4095.0f;
    sample->rain_level_pct = rain_level_pct_from_raw(raw);
    sample->is_raining = sample->rain_level_pct >= RAIN_SENSOR_ACTIVE_THRESHOLD_PCT;
    return ESP_OK;
}
