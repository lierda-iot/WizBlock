#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    LTE_STATE_OFF = 0,
    LTE_STATE_POWERING_ON,
    LTE_STATE_READY,
    LTE_STATE_ERROR,
} lte_state_t;

esp_err_t lte_hal_init(void);
esp_err_t lte_hal_power_on(void);
esp_err_t lte_hal_power_off(void);
lte_state_t lte_hal_get_state(void);
