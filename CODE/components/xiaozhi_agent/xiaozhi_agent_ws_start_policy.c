#include "xiaozhi_agent_ws_start_policy.h"

xiaozhi_agent_ws_start_action_t xiaozhi_agent_ws_start_decide(
    esp_err_t start_result, uint32_t attempt)
{
    if (ESP_OK == start_result) {
        return XIAOZHI_AGENT_WS_START_SUCCEEDED;
    }
    if (0U == attempt || XIAOZHI_AGENT_WS_START_MAX_ATTEMPTS <= attempt) {
        return XIAOZHI_AGENT_WS_START_GIVE_UP;
    }
    return XIAOZHI_AGENT_WS_START_RETRY;
}
