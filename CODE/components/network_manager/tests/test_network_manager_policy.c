#include "network_manager_policy.h"

int main(void)
{
    network_manager_policy_state_t state;
    network_manager_policy_output_t output;
    const network_manager_policy_input_t input = {
        .type = NETWORK_MANAGER_POLICY_INPUT_DISCONNECT_REQUEST,
    };

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_WIFI_ONLY);
    state.started = true;
    state.stable_ready = true;
    state.stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI;
    state.wifi_deadline_armed = true;

    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &input, &output)) {
        return 1;
    }
    if (!state.manual_offline || !state.disconnect_in_progress) {
        return 2;
    }
    if (state.stable_ready ||
        NETWORK_MANAGER_INTERFACE_NONE != state.stable_active_interface) {
        return 3;
    }
    if (state.wifi_deadline_armed) {
        return 4;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI != output.effect) {
        return 5;
    }
    if (!output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_ACCEPTED != output.operation_status) {
        return 6;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_4G_ONLY);
    state.started = true;
    state.stable_ready = true;
    state.stable_active_interface = NETWORK_MANAGER_INTERFACE_4G;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &input, &output)) {
        return 7;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE != output.effect) {
        return 8;
    }
    if (!state.manual_offline || !state.disconnect_in_progress) {
        return 9;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_DUAL_AUTO);
    state.started = true;
    state.stable_ready = true;
    state.stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &input, &output)) {
        return 10;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_REPORT_WIFI_DISCONNECTED !=
        output.effect) {
        return 11;
    }
    if (!state.manual_offline || !state.disconnect_in_progress) {
        return 12;
    }

    const network_manager_policy_input_t effect_succeeded = {
        .type = NETWORK_MANAGER_POLICY_INPUT_EFFECT_SUCCEEDED,
    };
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 13;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI != output.effect ||
        output.operation_status_valid) {
        return 14;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 15;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE != output.effect ||
        output.operation_status_valid) {
        return 16;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 17;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        !output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_COMPLETED != output.operation_status) {
        return 18;
    }
    if (state.disconnect_in_progress || !state.manual_offline ||
        NETWORK_MANAGER_POLICY_OPERATION_NONE != state.operation) {
        return 19;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_DUAL_AUTO);
    state.started = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &input, &output)) {
        return 20;
    }
    const network_manager_policy_input_t effect_failed = {
        .type = NETWORK_MANAGER_POLICY_INPUT_EFFECT_FAILED,
        .source_error = -1,
    };
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_failed, &output)) {
        return 21;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI != output.effect ||
        !state.operation_failed) {
        return 22;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE != output.effect) {
        return 23;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 24;
    }
    if (!output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_FAILED != output.operation_status ||
        state.disconnect_in_progress || !state.manual_offline) {
        return 25;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_WIFI_ONLY);
    state.started = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &input, &output)) {
        return 26;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &input, &output)) {
        return 27;
    }
    if (!output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_ALREADY_IN_PROGRESS !=
            output.operation_status ||
        NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI !=
            state.pending_effect) {
        return 28;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 29;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &input, &output)) {
        return 30;
    }
    if (!output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_NO_ACTION != output.operation_status ||
        NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        state.disconnect_in_progress) {
        return 31;
    }

    const network_manager_policy_input_t reconnect = {
        .type = NETWORK_MANAGER_POLICY_INPUT_RECONNECT_REQUEST,
    };
    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_WIFI_ONLY);
    state.started = true;
    state.current_wifi_config_present = true;
    state.manual_offline = true;
    state.stable_ready = true;
    state.stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI;
    state.wifi_deadline_armed = true;
    state.wifi_retry_attempt = NETWORK_MANAGER_WIFI_RETRY_LIMIT;
    state.wifi_retry_exhausted = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output)) {
        return 32;
    }
    if (state.manual_offline || !state.reconnect_in_progress ||
        state.disconnect_in_progress || state.stable_ready ||
        NETWORK_MANAGER_INTERFACE_NONE != state.stable_active_interface) {
        return 33;
    }
    if (0U != state.wifi_retry_attempt ||
        state.wifi_retry_exhausted || state.wifi_deadline_armed) {
        return 34;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG != output.effect ||
        !output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_ACCEPTED != output.operation_status) {
        return 35;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output)) {
        return 36;
    }
    if (!output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_ALREADY_IN_PROGRESS !=
            output.operation_status ||
        NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG !=
            state.pending_effect) {
        return 37;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 38;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_CONNECT_WIFI != output.effect ||
        output.operation_status_valid) {
        return 39;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 40;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        output.operation_status_valid || !state.reconnect_in_progress) {
        return 41;
    }
    const network_manager_policy_input_t wifi_ready = {
        .type = NETWORK_MANAGER_POLICY_INPUT_STABLE_READY,
        .interface = NETWORK_MANAGER_INTERFACE_WIFI,
        .value = true,
    };
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &wifi_ready, &output)) {
        return 42;
    }
    if (!state.stable_ready ||
        NETWORK_MANAGER_INTERFACE_WIFI != state.stable_active_interface ||
        state.reconnect_in_progress ||
        NETWORK_MANAGER_POLICY_OPERATION_NONE != state.operation) {
        return 43;
    }
    if (!output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_COMPLETED != output.operation_status) {
        return 44;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_WIFI_ONLY);
    state.started = true;
    state.manual_offline = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output)) {
        return 45;
    }
    if (state.manual_offline || state.reconnect_in_progress ||
        NETWORK_MANAGER_POLICY_OPERATION_NONE != state.operation) {
        return 46;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        !output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_FAILED != output.operation_status) {
        return 47;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_4G_ONLY);
    state.started = true;
    state.stable_ready = true;
    state.stable_active_interface = NETWORK_MANAGER_INTERFACE_4G;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output)) {
        return 48;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE != output.effect) {
        return 49;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 50;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE != output.effect) {
        return 51;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output)) {
        return 52;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        !state.reconnect_in_progress) {
        return 53;
    }
    const network_manager_policy_input_t cellular_ready = {
        .type = NETWORK_MANAGER_POLICY_INPUT_STABLE_READY,
        .interface = NETWORK_MANAGER_INTERFACE_4G,
        .value = true,
    };
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &cellular_ready, &output)) {
        return 54;
    }
    if (!output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_COMPLETED != output.operation_status ||
        state.reconnect_in_progress) {
        return 55;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_DUAL_AUTO);
    state.started = true;
    state.current_wifi_config_present = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output)) {
        return 56;
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG != output.effect) {
        return 57;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_CONNECT_WIFI != output.effect) {
        return 58;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE != output.effect) {
        return 59;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE != output.effect) {
        return 60;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        !state.reconnect_in_progress) {
        return 61;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &wifi_ready, &output) ||
        !output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_COMPLETED != output.operation_status) {
        return 62;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_DUAL_AUTO);
    state.started = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE != output.effect) {
        return 63;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE != output.effect) {
        return 64;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        !state.reconnect_in_progress) {
        return 65;
    }
    const network_manager_policy_input_t reconnect_failed = {
        .type = NETWORK_MANAGER_POLICY_INPUT_RECONNECT_TERMINAL_FAILURE,
    };
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect_failed, &output)) {
        return 66;
    }
    if (!output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_FAILED != output.operation_status ||
        state.reconnect_in_progress ||
        NETWORK_MANAGER_POLICY_OPERATION_NONE != state.operation) {
        return 67;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_DUAL_AUTO);
    state.started = true;
    state.current_wifi_config_present = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output)) {
        return 68;
    }
    if (NETWORK_MANAGER_POLICY_INVALID_STATE !=
        network_manager_policy_apply(&state, &input, &output)) {
        return 69;
    }
    if (NETWORK_MANAGER_POLICY_OPERATION_RECONNECT != state.operation ||
        NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG !=
            state.pending_effect ||
        state.disconnect_in_progress || !state.reconnect_in_progress) {
        return 70;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_DUAL_AUTO);
    state.started = true;
    state.current_wifi_config_present = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG != output.effect) {
        return 71;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_failed, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE != output.effect ||
        !state.wifi_path_failed || !state.reconnect_in_progress) {
        return 72;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE != output.effect) {
        return 73;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_succeeded, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect ||
        !state.reconnect_in_progress) {
        return 74;
    }

    network_manager_policy_init(&state, NETWORK_MANAGER_MODE_4G_ONLY);
    state.started = true;
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &reconnect, &output) ||
        NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE != output.effect) {
        return 75;
    }
    if (NETWORK_MANAGER_POLICY_OK !=
        network_manager_policy_apply(&state, &effect_failed, &output) ||
        !output.operation_status_valid ||
        NETWORK_MANAGER_OPERATION_FAILED != output.operation_status ||
        state.reconnect_in_progress) {
        return 76;
    }
    return 0;
}
