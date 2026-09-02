#include "companion_controller_stop_policy.h"

#include <stddef.h>

esp_err_t companion_controller_stop_plan_build(
    companion_controller_stop_reason_t reason,
    companion_controller_stop_plan_t *plan)
{
    if (NULL == plan || COMPANION_CONTROLLER_STOP_SESSION > reason ||
        COMPANION_CONTROLLER_STOP_AGENT_PLANE < reason) {
        return ESP_ERR_INVALID_ARG;
    }
    plan->agent_action =
        (COMPANION_CONTROLLER_STOP_REPLACEMENT_WAKE == reason) ?
        COMPANION_CONTROLLER_AGENT_STOP_RETIRE_BINDING :
        COMPANION_CONTROLLER_AGENT_STOP_CANCEL_REQUEST;
    plan->cancel_doa = COMPANION_CONTROLLER_STOP_AGENT_PLANE != reason;
    plan->stop_motion = COMPANION_CONTROLLER_STOP_AGENT_PLANE != reason;
    return ESP_OK;
}

esp_err_t companion_controller_agent_stop_execute(
    companion_controller_stop_reason_t reason,
    const companion_controller_agent_stop_ops_t *ops,
    void *user_ctx)
{
    if (NULL == ops || NULL == ops->retire_controller_binding ||
        NULL == ops->stop_audio) {
        return ESP_ERR_INVALID_ARG;
    }
    companion_controller_stop_plan_t plan = {0};
    const esp_err_t plan_result =
        companion_controller_stop_plan_build(reason, &plan);
    if (ESP_OK != plan_result) {
        return plan_result;
    }
    if (COMPANION_CONTROLLER_AGENT_STOP_RETIRE_BINDING ==
            plan.agent_action && NULL == ops->retire_binding) {
        return ESP_ERR_INVALID_ARG;
    }
    if (COMPANION_CONTROLLER_AGENT_STOP_CANCEL_REQUEST ==
            plan.agent_action && NULL == ops->cancel_request) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t first_error = ops->retire_controller_binding(user_ctx);
    const esp_err_t agent_result =
        (COMPANION_CONTROLLER_AGENT_STOP_RETIRE_BINDING == plan.agent_action) ?
        ops->retire_binding(user_ctx) : ops->cancel_request(user_ctx);
    if (ESP_OK == first_error && ESP_OK != agent_result) {
        first_error = agent_result;
    }
    const esp_err_t audio_result = ops->stop_audio(user_ctx);
    if (ESP_OK == first_error && ESP_OK != audio_result) {
        first_error = audio_result;
    }
    return first_error;
}

esp_err_t companion_controller_wake_stop_reason(
    companion_product_state_t source_state,
    companion_controller_stop_reason_t *reason)
{
    if (NULL == reason) {
        return ESP_ERR_INVALID_ARG;
    }
    if (COMPANION_PRODUCT_IDLE == source_state) {
        *reason = COMPANION_CONTROLLER_STOP_SESSION;
        return ESP_OK;
    }
    if (COMPANION_PRODUCT_CONNECTING == source_state ||
        COMPANION_PRODUCT_LISTENING == source_state ||
        COMPANION_PRODUCT_PROCESSING == source_state ||
        COMPANION_PRODUCT_SPEAKING == source_state) {
        *reason = COMPANION_CONTROLLER_STOP_REPLACEMENT_WAKE;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}
