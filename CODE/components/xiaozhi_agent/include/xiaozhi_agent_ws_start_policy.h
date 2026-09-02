#pragma once

#include "esp_err.h"

#include <stdint.h>

#define XIAOZHI_AGENT_WS_START_MAX_ATTEMPTS 3U

typedef enum {
    XIAOZHI_AGENT_WS_START_SUCCEEDED = 0,
    XIAOZHI_AGENT_WS_START_RETRY,
    XIAOZHI_AGENT_WS_START_GIVE_UP,
} xiaozhi_agent_ws_start_action_t;

xiaozhi_agent_ws_start_action_t xiaozhi_agent_ws_start_decide(
    esp_err_t start_result, uint32_t attempt);
