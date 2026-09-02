#pragma once

#include "esp_err.h"

typedef enum {
    XIAOZHI_UI_STATE_LTE_CONNECTING,
    XIAOZHI_UI_STATE_NETWORK_OK,
    XIAOZHI_UI_STATE_AGENT_READY,
    XIAOZHI_UI_STATE_NETWORK_LOST,
    XIAOZHI_UI_STATE_LISTENING,
    XIAOZHI_UI_STATE_SPEAKING,
} xiaozhi_ui_state_t;

esp_err_t xiaozhi_ui_init(void);
void xiaozhi_ui_set_state(xiaozhi_ui_state_t state);
