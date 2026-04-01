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
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_cfg), TAG, "battery adc config failed");

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

// EMA 滤波状态（跨调用保持）
// alpha=0.1：新值权重 10%，旧值权重 90%，时间常数约 30s（3s 采样周期下）
#define BATTERY_OVERSAMPLE_COUNT 16
#define BATTERY_EMA_ALPHA        0.1f

static float s_battery_ema_voltage = 0.0f;
static bool  s_battery_ema_init    = false;

esp_err_t analog_sensor_read_battery(battery_voltage_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        ESP_RETURN_ON_ERROR(analog_sensor_init(), TAG, "adc init failed");
    }
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "adc not ready");

    // 第一层：16 次过采样求均值，消除随机 ADC 抖动
    int32_t sum = 0;
    for (int i = 0; i < BATTERY_OVERSAMPLE_COUNT; i++) {
        int raw = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &raw), TAG, "battery adc read failed");
        sum += raw;
    }
    int avg_raw = (int)(sum / BATTERY_OVERSAMPLE_COUNT);

    // 分压比：R2/(R1+R2) = 7500/(30000+7500) = 0.2
    // 还原电池电压：V_batt = V_adc * (R1+R2)/R2 = V_adc * 5.0
    float v_adc = (float)avg_raw / 4095.0f * 3.3f;
    float voltage_raw = v_adc * (30000.0f + 7500.0f) / 7500.0f;

    // 第二层：EMA 指数滑动平均，平滑跨周期波动
    if (!s_battery_ema_init) {
        s_battery_ema_voltage = voltage_raw;
        s_battery_ema_init = true;
    } else {
        s_battery_ema_voltage = BATTERY_EMA_ALPHA * voltage_raw
                              + (1.0f - BATTERY_EMA_ALPHA) * s_battery_ema_voltage;
    }

    // 单节锂电池百分比（3.0V 空 ~ 4.2V 满）
    float pct = (s_battery_ema_voltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    sample->ready     = true;
    sample->raw_value = avg_raw;
    sample->voltage_v = s_battery_ema_voltage;
    sample->percent   = pct;
    return ESP_OK;
}
