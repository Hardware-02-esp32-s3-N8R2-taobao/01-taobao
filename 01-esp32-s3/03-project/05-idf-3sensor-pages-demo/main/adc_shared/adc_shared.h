#ifndef ADC_SHARED_H
#define ADC_SHARED_H

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

esp_err_t adc_shared_get_handle(adc_unit_t unit_id, adc_oneshot_unit_handle_t *handle);

#endif
