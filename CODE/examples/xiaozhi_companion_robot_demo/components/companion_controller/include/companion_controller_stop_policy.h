#pragma once

#include "companion_core.h"
#include "esp_err.h"

#include <stdbool.h>

typedef enum {
    COMPANION_CONTROLLER_STOP_SESSION = 0,
    COMPANION_CONTROLLER_STOP_REPLACEMENT_WAKE,
    COMPANION_CONTROLLER_STOP_AGENT_PLANE,
} companion_controller_stop_reason_t;

typedef enum {
    COMPANION_CONTROLLER_AGENT_STOP_CANCEL_REQUEST = 0,
    COMPANION_CONTROLLER_AGENT_STOP_RETIRE_BINDING,
} companion_controller_agent_stop_action_t;

typedef struct {
    companion_controller_agent_stop_action_t agent_action;
    bool cancel_doa;
    bool stop_motion;
} companion_controller_stop_plan_t;

typedef esp_err_t (*companion_controller_agent_stop_fn_t)(void *user_ctx);

typedef struct {
    companion_controller_agent_stop_fn_t retire_controller_binding;
    companion_controller_agent_stop_fn_t retire_binding;
    companion_controller_agent_stop_fn_t cancel_request;
    companion_controller_agent_stop_fn_t stop_audio;
} companion_controller_agent_stop_ops_t;

esp_err_t companion_controller_stop_plan_build(
    companion_controller_stop_reason_t reason,
    companion_controller_stop_plan_t *plan);
esp_err_t companion_controller_agent_stop_execute(
    companion_controller_stop_reason_t reason,
    const companion_controller_agent_stop_ops_t *ops,
    void *user_ctx);
esp_err_t companion_controller_wake_stop_reason(
    companion_product_state_t source_state,
    companion_controller_stop_reason_t *reason);
