#pragma once

/** Pure cellular observation model. */

#include "network_manager.h"

#include <stdbool.h>
#include <stdint.h>

#include "network_manager_tuning.h"

typedef struct {
    network_manager_4g_phase_t phase;
    bool raw_link_up;
    bool raw_ipv4_ready;
    bool internet_reachable;
    bool ever_ipv4_ready;
    bool initial_wait_active;
    uint32_t initial_wait_since_ms;
} network_manager_cellular_runtime_model_t;

typedef struct {
    bool initial_ipv4_timeout;
    bool recovered;
    bool raw_changed;
} network_manager_cellular_runtime_output_t;

/** Reset all observed state. */
void network_manager_cellular_runtime_model_init(
    network_manager_cellular_runtime_model_t *model);
/** Start the one-time initial IPv4 observation window after manager init. */
void network_manager_cellular_runtime_model_manager_initialized(
    network_manager_cellular_runtime_model_t *model,
    uint32_t now_ms,
    network_manager_cellular_runtime_output_t *output);
/** Advance the one-time initial IPv4 observation deadline. */
void network_manager_cellular_runtime_model_tick(
    network_manager_cellular_runtime_model_t *model,
    uint32_t now_ms,
    network_manager_cellular_runtime_output_t *output);
/** Apply current cellular link/IP/internet facts. */
void network_manager_cellular_runtime_model_update_raw(
    network_manager_cellular_runtime_model_t *model,
    bool link_up,
    bool ipv4_ready,
    bool internet_reachable,
    uint32_t now_ms,
    network_manager_cellular_runtime_output_t *output);
