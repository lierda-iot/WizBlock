#pragma once

#include "esp_err.h"

#include <stdint.h>

typedef esp_err_t (*companion_controller_wake_effect_fn_t)(
    uint32_t generation, uint32_t wake_seq, void *user_ctx);

typedef struct {
    companion_controller_wake_effect_fn_t start_agent;
    companion_controller_wake_effect_fn_t start_motion;
} companion_controller_wake_effect_ops_t;

typedef struct {
    esp_err_t agent_result;
    esp_err_t motion_result;
} companion_controller_wake_effect_result_t;

esp_err_t companion_controller_wake_effects_execute(
    const companion_controller_wake_effect_ops_t *ops,
    uint32_t generation, uint32_t wake_seq, void *user_ctx,
    companion_controller_wake_effect_result_t *result);
