#include "analog_sensor.h"

#include "esp_check.h"
#include "esp_adc/adc_oneshot.h"

#include "app_config.h"

#define TAG "analog_sensor"

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static bool s_ready = false;

static esp_err_t read_channel(adc_channel_t channel, analog_sensor_sample_t *sample)
{
    int raw = 0;
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "adc not ready");
    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc_handle, channel, &raw), TAG, "adc read failed");
    sample->ready = true;
    sample->raw_value = raw;
    sample->percent = ((float)raw / 4095.0f) * 100.0f;
    return ESP_OK;
}

esp_err_t analog_sensor_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_cfg, &s_adc_handle), TAG, "adc unit init failed");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_1, &chan_cfg), TAG, "soil adc config failed");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_cfg), TAG, "rain adc config failed");

    s_ready = true;
    return ESP_OK;
}

esp_err_t analog_sensor_read_soil(analog_sensor_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        ESP_RETURN_ON_ERROR(analog_sensor_init(), TAG, "adc init failed");
    }
    return read_channel(ADC_CHANNEL_1, sample);
}

esp_err_t analog_sensor_read_rain(analog_sensor_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        ESP_RETURN_ON_ERROR(analog_sensor_init(), TAG, "adc init failed");
    }
    return read_channel(ADC_CHANNEL_0, sample);
}
