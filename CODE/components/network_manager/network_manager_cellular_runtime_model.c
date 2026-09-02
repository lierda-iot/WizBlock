/* Pure passive cellular observation model. */
#include "network_manager_cellular_runtime_model.h"

#include <stddef.h>

static void clear_output(network_manager_cellular_runtime_output_t *output)
{
    *output = (network_manager_cellular_runtime_output_t){0};
}

static bool elapsed(uint32_t now_ms,
                    uint32_t since_ms,
                    uint32_t duration_ms)
{
    /* Unsigned subtraction keeps deadline checks valid across tick wrap. */
    return (uint32_t)(now_ms - since_ms) >= duration_ms;
}

void network_manager_cellular_runtime_model_init(
    network_manager_cellular_runtime_model_t *model)
{
    if (NULL == model) {
        return;
    }
    *model = (network_manager_cellular_runtime_model_t){
        .phase = NETWORK_MANAGER_4G_DISABLED,
    };
}

void network_manager_cellular_runtime_model_manager_initialized(
    network_manager_cellular_runtime_model_t *model,
    uint32_t now_ms,
    network_manager_cellular_runtime_output_t *output)
{
    if (NULL == model || NULL == output) {
        return;
    }
    clear_output(output);
    /* Manager init is unique; this starts only the initial observation window. */
    model->phase = NETWORK_MANAGER_4G_WAIT_LINK;
    model->initial_wait_active = true;
    model->initial_wait_since_ms = now_ms;
}

void network_manager_cellular_runtime_model_tick(
    network_manager_cellular_runtime_model_t *model,
    uint32_t now_ms,
    network_manager_cellular_runtime_output_t *output)
{
    if (NULL == model || NULL == output) {
        return;
    }
    clear_output(output);
    if (model->initial_wait_active &&
        elapsed(now_ms,
                model->initial_wait_since_ms,
                NETWORK_MANAGER_CELLULAR_INITIAL_IPV4_WAIT_MS)) {
        model->initial_wait_active = false;
        /* Stay passive after first-boot timeout; do not schedule a power cycle. */
        output->initial_ipv4_timeout = true;
    }
}

void network_manager_cellular_runtime_model_update_raw(
    network_manager_cellular_runtime_model_t *model,
    bool link_up,
    bool ipv4_ready,
    bool internet_reachable,
    uint32_t now_ms,
    network_manager_cellular_runtime_output_t *output)
{
    if (NULL == model || NULL == output) {
        return;
    }
    clear_output(output);
    (void)now_ms;
    const bool raw_changed = model->raw_link_up != link_up ||
                             model->raw_ipv4_ready != ipv4_ready;
    const bool was_ipv4_ready = model->raw_ipv4_ready;
    model->raw_link_up = link_up;
    model->raw_ipv4_ready = ipv4_ready;
    model->internet_reachable = internet_reachable;
    output->raw_changed = raw_changed;

    if (ipv4_ready) {
        /* A real IPv4 report completes the initial observation window. */
        model->ever_ipv4_ready = true;
        model->initial_wait_active = false;
        model->phase = internet_reachable ?
            NETWORK_MANAGER_4G_READY : NETWORK_MANAGER_4G_WAIT_INTERNET;
        output->recovered = !was_ipv4_ready;
        return;
    }

    model->phase = link_up ?
        NETWORK_MANAGER_4G_WAIT_IPV4 : NETWORK_MANAGER_4G_WAIT_LINK;
}
