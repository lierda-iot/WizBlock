#pragma once

/** Pure Wi-Fi raw/stable debounce and bounded-retry model. */

#include <stdbool.h>
#include <stdint.h>

#include "network_manager_tuning.h"

typedef struct {
    bool raw_link_up;
    bool raw_ipv4_ready;
    bool internet_reachable;
    bool stable_link_up;
    bool stable_ipv4_ready;
    bool stable_ready;
    bool connect_stability_active;
    uint32_t connect_stability_since_ms;
    bool disconnect_stability_active;
    uint32_t disconnect_stability_since_ms;
    bool retry_reset_stability_active;
    uint32_t retry_reset_stability_since_ms;
    uint8_t retry_attempt;
    bool retry_exhausted;
    bool retry_deadline_active;
    uint32_t retry_deadline_since_ms;
    uint32_t retry_delay_ms;
    bool automatic_recovery_enabled;
} network_manager_wifi_runtime_model_t;

typedef struct {
    bool stable_changed;
    bool report_connected;
    bool report_disconnected;
    bool connect_retry;
    bool retry_exhausted_changed;
    bool retry_budget_reset;
} network_manager_wifi_runtime_output_t;

/** Reset all state and enable automatic recovery. */
void network_manager_wifi_runtime_model_init(
    network_manager_wifi_runtime_model_t *model);
/** Apply current raw link/IP/internet facts and arm stability windows. */
void network_manager_wifi_runtime_model_update_raw(
    network_manager_wifi_runtime_model_t *model,
    bool link_up,
    bool ipv4_ready,
    bool internet_reachable,
    uint32_t now_ms,
    network_manager_wifi_runtime_output_t *output);
/** Advance stability and retry deadlines using wrap-safe now_ms. */
void network_manager_wifi_runtime_model_tick(
    network_manager_wifi_runtime_model_t *model,
    uint32_t now_ms,
    network_manager_wifi_runtime_output_t *output);
/** Consume one bounded retry and schedule its backoff deadline. */
void network_manager_wifi_runtime_model_on_connect_failed(
    network_manager_wifi_runtime_model_t *model,
    uint32_t now_ms,
    network_manager_wifi_runtime_output_t *output);
/** Clear the current retry budget without changing raw/stable facts. */
void network_manager_wifi_runtime_model_reset_retry(
    network_manager_wifi_runtime_model_t *model);
/** Enable or suppress automatic deadlines; disabling cancels pending ones. */
void network_manager_wifi_runtime_model_set_automatic_recovery(
    network_manager_wifi_runtime_model_t *model,
    bool enabled);
