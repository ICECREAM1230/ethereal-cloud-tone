#pragma once
#include "esp_err.h"
typedef int adc_unit_t;
typedef int adc_channel_t;
typedef void *adc_oneshot_unit_handle_t;
#define ADC_UNIT_1 0
#define ADC_ATTEN_DB_12 3
#define ADC_BITWIDTH_DEFAULT 0
typedef struct { adc_unit_t unit_id; } adc_oneshot_unit_init_cfg_t;
typedef struct { int atten, bitwidth; } adc_oneshot_chan_cfg_t;
esp_err_t adc_oneshot_io_to_channel(int gpio, adc_unit_t *unit, adc_channel_t *channel);
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *config, adc_oneshot_unit_handle_t *adc);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t adc, adc_channel_t channel,
                                    const adc_oneshot_chan_cfg_t *config);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t adc, adc_channel_t channel, int *sample);
