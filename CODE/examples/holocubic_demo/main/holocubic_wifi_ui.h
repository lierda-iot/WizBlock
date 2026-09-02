#pragma once

#include "esp_err.h"

#include "holocubic_ui_state.h"

#include <stdbool.h>

esp_err_t holocubic_wifi_ui_start(holocubic_ui_state_t *state);
bool holocubic_wifi_ui_is_active(void);
void holocubic_wifi_ui_open(void);
