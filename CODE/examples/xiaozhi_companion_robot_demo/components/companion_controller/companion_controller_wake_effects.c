#include "companion_controller_wake_effects.h"

#include <stddef.h>

esp_err_t companion_controller_wake_effects_execute(
    const companion_controller_wake_effect_ops_t *ops,
    uint32_t generation, uint32_t wake_seq, void *user_ctx,
    companion_controller_wake_effect_result_t *result)
{
    if (NULL == ops || NULL == ops->start_agent ||
        NULL == ops->start_motion || 0U == generation || 0U == wake_seq ||
        NULL == result) {
        return ESP_ERR_INVALID_ARG;
    }

    result->agent_result = ops->start_agent(generation, wake_seq, user_ctx);
    result->motion_result = ops->start_motion(generation, wake_seq, user_ctx);
    return ESP_OK;
}
