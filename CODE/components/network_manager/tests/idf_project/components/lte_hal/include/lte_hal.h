#pragma once

#include "esp_err.h"

esp_err_t lte_hal_init(void);
esp_err_t lte_hal_power_on(void);
esp_err_t lte_hal_power_off(void);
