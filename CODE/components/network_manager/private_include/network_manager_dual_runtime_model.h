#pragma once

/** Pure aggregation of per-path facts into component-wide readiness. */

#include "network_manager.h"

#include <stdbool.h>

typedef struct {
    network_manager_mode_t mode;
    bool manual_offline;
    bool wifi_config_present;
    bool wifi_raw_link_up;
    bool wifi_raw_ipv4_ready;
    bool wifi_stable_ready;
    bool wifi_retry_exhausted;
    bool cellular_raw_link_up;
    bool cellular_raw_ipv4_ready;
    bool cellular_stable_ready;
    bool internet_reachable;
    network_manager_interface_t raw_active_interface;
    network_manager_interface_t previous_stable_active_interface;
} network_manager_dual_runtime_input_t;

typedef struct {
    network_manager_interface_t raw_active_interface;
    network_manager_interface_t stable_active_interface;
    bool raw_ready;
    bool stable_ready;
    bool interface_switch_in_progress;
    bool all_retry_exhausted;
} network_manager_dual_runtime_output_t;

/** Reduce one complete fact set into raw/stable route and exhaustion state. */
void network_manager_dual_runtime_model_reduce(
    const network_manager_dual_runtime_input_t *input,
    network_manager_dual_runtime_output_t *output);
