#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

esp_err_t board_adc_init(void);
adc_oneshot_unit_handle_t board_adc_handle(void);
