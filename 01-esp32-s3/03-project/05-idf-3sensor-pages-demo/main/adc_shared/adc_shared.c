#include "adc_shared/adc_shared.h"

#include <stdbool.h>

#include "esp_check.h"
#include "soc/soc_caps.h"

typedef struct {
    bool initialized;
    adc_oneshot_unit_handle_t handle;
} adc_shared_unit_t;

static adc_shared_unit_t s_units[SOC_ADC_PERIPH_NUM];

esp_err_t adc_shared_get_handle(adc_unit_t unit_id, adc_oneshot_unit_handle_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (unit_id < 0 || unit_id >= SOC_ADC_PERIPH_NUM) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_units[unit_id].initialized) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = unit_id,
        };

        ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_units[unit_id].handle), "adc_shared", "adc unit init failed");
        s_units[unit_id].initialized = true;
    }

    *handle = s_units[unit_id].handle;
    return ESP_OK;
}
