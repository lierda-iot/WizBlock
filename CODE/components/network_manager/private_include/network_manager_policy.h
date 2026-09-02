#pragma once

/** Pure reducer contract for serialized disconnect/reconnect effects. */

#include "network_manager.h"

#include <stdbool.h>
#include <stdint.h>

#include "network_manager_tuning.h"

typedef enum {
    NETWORK_MANAGER_POLICY_OK = 0,
    NETWORK_MANAGER_POLICY_INVALID_ARGUMENT = 1,
    NETWORK_MANAGER_POLICY_INVALID_MODE = 2,
    NETWORK_MANAGER_POLICY_INVALID_STATE = 3,
    NETWORK_MANAGER_POLICY_UNSUPPORTED_INPUT = 4,
} network_manager_policy_result_t;

typedef enum {
    NETWORK_MANAGER_POLICY_INPUT_START = 0,
    NETWORK_MANAGER_POLICY_INPUT_WIFI_CONFIG_AVAILABLE = 1,
    NETWORK_MANAGER_POLICY_INPUT_WIFI_CONFIG_MISSING = 2,
    NETWORK_MANAGER_POLICY_INPUT_WIFI_LINK_UP = 3,
    NETWORK_MANAGER_POLICY_INPUT_WIFI_LINK_DOWN = 4,
    NETWORK_MANAGER_POLICY_INPUT_WIFI_GOT_IP = 5,
    NETWORK_MANAGER_POLICY_INPUT_WIFI_LOST_IP = 6,
    NETWORK_MANAGER_POLICY_INPUT_CELLULAR_LINK_UP = 7,
    NETWORK_MANAGER_POLICY_INPUT_CELLULAR_LINK_DOWN = 8,
    NETWORK_MANAGER_POLICY_INPUT_CELLULAR_GOT_IP = 9,
    NETWORK_MANAGER_POLICY_INPUT_CELLULAR_LOST_IP = 10,
    NETWORK_MANAGER_POLICY_INPUT_INTERNET_READY_CHANGED = 11,
    NETWORK_MANAGER_POLICY_INPUT_TIMER_TICK = 12,
    NETWORK_MANAGER_POLICY_INPUT_EFFECT_SUCCEEDED = 13,
    NETWORK_MANAGER_POLICY_INPUT_EFFECT_FAILED = 14,
    NETWORK_MANAGER_POLICY_INPUT_RECONNECT_REQUEST = 15,
    NETWORK_MANAGER_POLICY_INPUT_DISCONNECT_REQUEST = 16,
    NETWORK_MANAGER_POLICY_INPUT_STABLE_READY = 17,
    NETWORK_MANAGER_POLICY_INPUT_RECONNECT_TERMINAL_FAILURE = 18,
} network_manager_policy_input_type_t;

typedef enum {
    NETWORK_MANAGER_POLICY_EFFECT_NONE = 0,
    NETWORK_MANAGER_POLICY_EFFECT_START_WIFI_DRIVER = 1,
    NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG = 2,
    NETWORK_MANAGER_POLICY_EFFECT_CONNECT_WIFI = 3,
    NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI = 4,
    NETWORK_MANAGER_POLICY_EFFECT_REPORT_WIFI_CONNECTED = 5,
    NETWORK_MANAGER_POLICY_EFFECT_REPORT_WIFI_DISCONNECTED = 6,
    NETWORK_MANAGER_POLICY_EFFECT_INIT_LTE = 7,
    NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE = 8,
    NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE = 9,
    NETWORK_MANAGER_POLICY_EFFECT_INIT_NET_MGMT = 10,
} network_manager_policy_effect_t;

typedef enum {
    NETWORK_MANAGER_POLICY_OPERATION_NONE = 0,
    NETWORK_MANAGER_POLICY_OPERATION_DISCONNECT = 1,
    NETWORK_MANAGER_POLICY_OPERATION_RECONNECT = 2,
} network_manager_policy_operation_t;

typedef struct {
    network_manager_policy_input_type_t type;
    uint32_t now_ms;
    network_manager_interface_t interface;
    bool value;
    int32_t source_error;
    int32_t raw_reason;
} network_manager_policy_input_t;

typedef struct {
    network_manager_mode_t mode;
    bool started;
    bool current_wifi_config_present;
    bool manual_offline;
    bool stable_ready;
    network_manager_interface_t stable_active_interface;
    bool reconnect_in_progress;
    bool disconnect_in_progress;
    bool wifi_deadline_armed;
    uint8_t wifi_retry_attempt;
    bool wifi_retry_exhausted;
    network_manager_policy_operation_t operation;
    uint8_t operation_step;
    network_manager_policy_effect_t pending_effect;
    bool operation_failed;
    bool wifi_path_failed;
    bool cellular_path_failed;
} network_manager_policy_state_t;

typedef struct {
    network_manager_policy_effect_t effect;
    bool operation_status_valid;
    network_manager_operation_status_t operation_status;
    bool snapshot_changed;
} network_manager_policy_output_t;

void network_manager_policy_init(network_manager_policy_state_t *state,
                                 network_manager_mode_t mode);
/**
 * Apply one normalized input and return at most one effect for the facade.
 * Effect success or failure must be fed back before applying another request.
 */
network_manager_policy_result_t network_manager_policy_apply(
    network_manager_policy_state_t *state,
    const network_manager_policy_input_t *input,
    network_manager_policy_output_t *output);
