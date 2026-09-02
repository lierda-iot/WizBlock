/*
 * Pure operation sequencer for disconnect and reconnect requests.
 *
 * This reducer chooses one external effect at a time. The facade executes the
 * effect and feeds success or failure back before the reducer advances, which
 * keeps mode-specific ordering independent of ESP-IDF and FreeRTOS.
 */
#include "network_manager_policy.h"

#include <stddef.h>

static bool mode_is_valid(network_manager_mode_t mode)
{
    return NETWORK_MANAGER_MODE_WIFI_ONLY == mode ||
           NETWORK_MANAGER_MODE_4G_ONLY == mode ||
           NETWORK_MANAGER_MODE_DUAL_AUTO == mode;
}

void network_manager_policy_init(network_manager_policy_state_t *state,
                                 network_manager_mode_t mode)
{
    if (NULL == state) {
        return;
    }
    *state = (network_manager_policy_state_t){
        .mode = mode,
        .stable_active_interface = NETWORK_MANAGER_INTERFACE_NONE,
        .operation = NETWORK_MANAGER_POLICY_OPERATION_NONE,
        .pending_effect = NETWORK_MANAGER_POLICY_EFFECT_NONE,
    };
}

network_manager_policy_result_t network_manager_policy_apply(
    network_manager_policy_state_t *state,
    const network_manager_policy_input_t *input,
    network_manager_policy_output_t *output)
{
    if (NULL == state || NULL == input || NULL == output) {
        return NETWORK_MANAGER_POLICY_INVALID_ARGUMENT;
    }
    *output = (network_manager_policy_output_t){
        .effect = NETWORK_MANAGER_POLICY_EFFECT_NONE,
    };
    if (!mode_is_valid(state->mode)) {
        return NETWORK_MANAGER_POLICY_INVALID_MODE;
    }
    if (!state->started) {
        return NETWORK_MANAGER_POLICY_INVALID_STATE;
    }
    if ((NETWORK_MANAGER_POLICY_INPUT_RECONNECT_REQUEST == input->type &&
         state->disconnect_in_progress) ||
        (NETWORK_MANAGER_POLICY_INPUT_DISCONNECT_REQUEST == input->type &&
         state->reconnect_in_progress)) {
        return NETWORK_MANAGER_POLICY_INVALID_STATE;
    }

    /* A reconnect starts a fresh budget and rebuilds every path in the mode. */
    if (NETWORK_MANAGER_POLICY_INPUT_RECONNECT_REQUEST == input->type) {
        if (state->reconnect_in_progress) {
            output->operation_status_valid = true;
            output->operation_status =
                NETWORK_MANAGER_OPERATION_ALREADY_IN_PROGRESS;
            return NETWORK_MANAGER_POLICY_OK;
        }
        state->manual_offline = false;
        state->stable_ready = false;
        state->stable_active_interface = NETWORK_MANAGER_INTERFACE_NONE;
        state->reconnect_in_progress = true;
        state->disconnect_in_progress = false;
        state->wifi_deadline_armed = false;
        state->wifi_retry_attempt = 0U;
        state->wifi_retry_exhausted = false;
        state->operation = NETWORK_MANAGER_POLICY_OPERATION_RECONNECT;
        state->operation_step = 0U;
        state->operation_failed = false;
        state->wifi_path_failed =
            NETWORK_MANAGER_MODE_DUAL_AUTO == state->mode &&
            !state->current_wifi_config_present;
        state->cellular_path_failed = false;
        if (NETWORK_MANAGER_MODE_WIFI_ONLY == state->mode &&
            !state->current_wifi_config_present) {
            state->reconnect_in_progress = false;
            state->operation = NETWORK_MANAGER_POLICY_OPERATION_NONE;
            state->pending_effect = NETWORK_MANAGER_POLICY_EFFECT_NONE;
            output->operation_status_valid = true;
            output->operation_status = NETWORK_MANAGER_OPERATION_FAILED;
            output->snapshot_changed = true;
            return NETWORK_MANAGER_POLICY_OK;
        }
        /* Dual mode may continue with LTE even when no Wi-Fi config exists. */
        if (NETWORK_MANAGER_MODE_4G_ONLY == state->mode ||
            (NETWORK_MANAGER_MODE_DUAL_AUTO == state->mode &&
             !state->current_wifi_config_present)) {
            state->pending_effect =
                NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE;
        } else {
            state->pending_effect =
                NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG;
        }

        output->effect = state->pending_effect;
        output->operation_status_valid = true;
        output->operation_status = NETWORK_MANAGER_OPERATION_ACCEPTED;
        output->snapshot_changed = true;
        return NETWORK_MANAGER_POLICY_OK;
    }
    /*
     * Effect feedback is the only way an operation advances. Failures mark the
     * affected path but do not skip an independent Dual-mode path.
     */
    if (NETWORK_MANAGER_POLICY_INPUT_EFFECT_SUCCEEDED == input->type ||
        NETWORK_MANAGER_POLICY_INPUT_EFFECT_FAILED == input->type) {
        if (NETWORK_MANAGER_POLICY_EFFECT_NONE == state->pending_effect) {
            return NETWORK_MANAGER_POLICY_INVALID_STATE;
        }
        if (NETWORK_MANAGER_POLICY_OPERATION_RECONNECT == state->operation) {
            const bool effect_failed =
                NETWORK_MANAGER_POLICY_INPUT_EFFECT_FAILED == input->type;
            const network_manager_policy_effect_t completed_effect =
                state->pending_effect;
            if (effect_failed) {
                state->operation_failed = true;
                if (NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG ==
                    completed_effect) {
                    state->wifi_path_failed = true;
                } else if (NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE ==
                               completed_effect ||
                           NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE ==
                               completed_effect) {
                    state->cellular_path_failed = true;
                }
            }
            ++state->operation_step;
            if (NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG ==
                    completed_effect &&
                !effect_failed) {
                state->pending_effect =
                    NETWORK_MANAGER_POLICY_EFFECT_CONNECT_WIFI;
            } else if ((NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG ==
                            completed_effect ||
                        NETWORK_MANAGER_POLICY_EFFECT_CONNECT_WIFI ==
                            completed_effect) &&
                       NETWORK_MANAGER_MODE_DUAL_AUTO == state->mode) {
                state->pending_effect =
                    NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE;
            } else if (NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE ==
                           completed_effect &&
                       !effect_failed) {
                state->pending_effect =
                    NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE;
            } else {
                state->pending_effect = NETWORK_MANAGER_POLICY_EFFECT_NONE;
            }

            const bool no_path_available =
                (NETWORK_MANAGER_MODE_WIFI_ONLY == state->mode &&
                 state->wifi_path_failed) ||
                (NETWORK_MANAGER_MODE_4G_ONLY == state->mode &&
                 state->cellular_path_failed) ||
                (NETWORK_MANAGER_MODE_DUAL_AUTO == state->mode &&
                 state->wifi_path_failed && state->cellular_path_failed);
            if (no_path_available) {
                state->reconnect_in_progress = false;
                state->operation = NETWORK_MANAGER_POLICY_OPERATION_NONE;
                state->pending_effect = NETWORK_MANAGER_POLICY_EFFECT_NONE;
                output->operation_status_valid = true;
                output->operation_status = NETWORK_MANAGER_OPERATION_FAILED;
                output->snapshot_changed = true;
            }
            output->effect = state->pending_effect;
            return NETWORK_MANAGER_POLICY_OK;
        }
        if (NETWORK_MANAGER_POLICY_OPERATION_DISCONNECT != state->operation) {
            return NETWORK_MANAGER_POLICY_INVALID_STATE;
        }
        if (NETWORK_MANAGER_POLICY_INPUT_EFFECT_FAILED == input->type) {
            state->operation_failed = true;
        }

        ++state->operation_step;
        /* Dual disconnect order: report Wi-Fi down, disconnect Wi-Fi, LTE off. */
        if (NETWORK_MANAGER_MODE_DUAL_AUTO == state->mode &&
            1U == state->operation_step) {
            state->pending_effect =
                NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI;
        } else if (NETWORK_MANAGER_MODE_DUAL_AUTO == state->mode &&
                   2U == state->operation_step) {
            state->pending_effect =
                NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE;
        } else {
            state->pending_effect = NETWORK_MANAGER_POLICY_EFFECT_NONE;
            state->operation = NETWORK_MANAGER_POLICY_OPERATION_NONE;
            state->disconnect_in_progress = false;
            output->operation_status_valid = true;
            output->operation_status = state->operation_failed ?
                NETWORK_MANAGER_OPERATION_FAILED :
                NETWORK_MANAGER_OPERATION_COMPLETED;
            output->snapshot_changed = true;
        }
        output->effect = state->pending_effect;
        return NETWORK_MANAGER_POLICY_OK;
    }
    /* Reconnect completes only after the facade reports a stable usable path. */
    if (NETWORK_MANAGER_POLICY_INPUT_STABLE_READY == input->type) {
        state->stable_ready = input->value;
        state->stable_active_interface = input->value ?
            input->interface : NETWORK_MANAGER_INTERFACE_NONE;
        output->snapshot_changed = true;
        if (input->value && state->reconnect_in_progress &&
            NETWORK_MANAGER_POLICY_EFFECT_NONE == state->pending_effect) {
            state->reconnect_in_progress = false;
            state->operation = NETWORK_MANAGER_POLICY_OPERATION_NONE;
            output->operation_status_valid = true;
            output->operation_status = NETWORK_MANAGER_OPERATION_COMPLETED;
        }
        return NETWORK_MANAGER_POLICY_OK;
    }
    if (NETWORK_MANAGER_POLICY_INPUT_RECONNECT_TERMINAL_FAILURE == input->type) {
        if (!state->reconnect_in_progress ||
            NETWORK_MANAGER_POLICY_OPERATION_RECONNECT != state->operation) {
            return NETWORK_MANAGER_POLICY_INVALID_STATE;
        }
        state->reconnect_in_progress = false;
        state->operation = NETWORK_MANAGER_POLICY_OPERATION_NONE;
        state->pending_effect = NETWORK_MANAGER_POLICY_EFFECT_NONE;
        output->operation_status_valid = true;
        output->operation_status = NETWORK_MANAGER_OPERATION_FAILED;
        output->snapshot_changed = true;
        return NETWORK_MANAGER_POLICY_OK;
    }
    if (NETWORK_MANAGER_POLICY_INPUT_DISCONNECT_REQUEST != input->type) {
        return NETWORK_MANAGER_POLICY_UNSUPPORTED_INPUT;
    }
    if (state->disconnect_in_progress) {
        output->operation_status_valid = true;
        output->operation_status =
            NETWORK_MANAGER_OPERATION_ALREADY_IN_PROGRESS;
        return NETWORK_MANAGER_POLICY_OK;
    }
    if (state->manual_offline) {
        output->operation_status_valid = true;
        output->operation_status = NETWORK_MANAGER_OPERATION_NO_ACTION;
        return NETWORK_MANAGER_POLICY_OK;
    }

    /* Manual offline is visible immediately, before physical effects finish. */
    state->manual_offline = true;
    state->stable_ready = false;
    state->stable_active_interface = NETWORK_MANAGER_INTERFACE_NONE;
    state->disconnect_in_progress = true;
    state->wifi_deadline_armed = false;
    state->operation = NETWORK_MANAGER_POLICY_OPERATION_DISCONNECT;
    state->operation_step = 0U;
    state->operation_failed = false;
    if (NETWORK_MANAGER_MODE_WIFI_ONLY == state->mode) {
        state->pending_effect =
            NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI;
    } else if (NETWORK_MANAGER_MODE_4G_ONLY == state->mode) {
        state->pending_effect =
            NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE;
    } else {
        state->pending_effect =
            NETWORK_MANAGER_POLICY_EFFECT_REPORT_WIFI_DISCONNECTED;
    }

    output->effect = state->pending_effect;
    output->operation_status_valid = true;
    output->operation_status = NETWORK_MANAGER_OPERATION_ACCEPTED;
    output->snapshot_changed = true;
    return NETWORK_MANAGER_POLICY_OK;
}
