/* Pure aggregation of per-path facts into component-wide readiness. */
#include "network_manager_dual_runtime_model.h"

#include <stddef.h>

static bool mode_is_valid(network_manager_mode_t mode)
{
    return NETWORK_MANAGER_MODE_WIFI_ONLY == mode ||
           NETWORK_MANAGER_MODE_4G_ONLY == mode ||
           NETWORK_MANAGER_MODE_DUAL_AUTO == mode;
}

void network_manager_dual_runtime_model_reduce(
    const network_manager_dual_runtime_input_t *input,
    network_manager_dual_runtime_output_t *output)
{
    if (NULL == input || NULL == output) {
        return;
    }
    *output = (network_manager_dual_runtime_output_t){
        .raw_active_interface = input->raw_active_interface,
    };
    if (!mode_is_valid(input->mode)) {
        return;
    }

    const bool wifi_enabled =
        NETWORK_MANAGER_MODE_4G_ONLY != input->mode;
    const bool cellular_enabled =
        NETWORK_MANAGER_MODE_WIFI_ONLY != input->mode;
    const bool wifi_raw_ready = wifi_enabled && input->wifi_raw_link_up &&
                                input->wifi_raw_ipv4_ready;
    const bool cellular_raw_ready = cellular_enabled &&
                                    input->cellular_raw_link_up &&
                                    input->cellular_raw_ipv4_ready;
    if (NETWORK_MANAGER_INTERFACE_WIFI == input->raw_active_interface) {
        output->raw_ready = wifi_raw_ready || cellular_raw_ready;
    } else if (NETWORK_MANAGER_INTERFACE_4G ==
               input->raw_active_interface) {
        output->raw_ready = cellular_raw_ready || wifi_raw_ready;
    } else {
        output->raw_ready = wifi_raw_ready || cellular_raw_ready;
    }
    output->raw_ready = output->raw_ready && input->internet_reachable;
    if (input->manual_offline) {
        /* Raw facts remain observable, but manual offline can never be ready. */
        output->interface_switch_in_progress =
            output->raw_active_interface !=
            output->stable_active_interface;
        return;
    }

    if (input->internet_reachable) {
        /*
         * Prefer the raw active interface only while its raw path is usable.
         * lsd_net_mgmt can publish the old Wi-Fi interface briefly after an
         * AP disappears; retaining Wi-Fi in that window delays the 4G
         * failover even though cellular is already ready.
         */
        if (NETWORK_MANAGER_INTERFACE_WIFI == input->raw_active_interface &&
            wifi_raw_ready && input->wifi_stable_ready) {
            output->stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI;
        } else if (NETWORK_MANAGER_INTERFACE_4G ==
                       input->raw_active_interface &&
                   cellular_enabled && input->cellular_stable_ready) {
            output->stable_active_interface = NETWORK_MANAGER_INTERFACE_4G;
        } else if (NETWORK_MANAGER_INTERFACE_WIFI ==
                       input->previous_stable_active_interface &&
                   wifi_raw_ready && input->wifi_stable_ready) {
            output->stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI;
        } else if (NETWORK_MANAGER_INTERFACE_4G ==
                       input->previous_stable_active_interface &&
                   cellular_enabled && input->cellular_stable_ready) {
            output->stable_active_interface = NETWORK_MANAGER_INTERFACE_4G;
        } else if (cellular_enabled && input->cellular_stable_ready) {
            /* Immediate fallback while the closed-source route lags behind. */
            output->stable_active_interface = NETWORK_MANAGER_INTERFACE_4G;
        }
    }
    output->stable_ready =
        NETWORK_MANAGER_INTERFACE_NONE != output->stable_active_interface;
    output->interface_switch_in_progress =
        output->raw_active_interface != output->stable_active_interface;

    /* Disabled, unavailable, or exhausted paths count as terminal. */
    const bool wifi_terminal = !wifi_enabled || !input->wifi_config_present ||
                               input->wifi_retry_exhausted;
    output->all_retry_exhausted = wifi_enabled &&
                                  !output->stable_ready && wifi_terminal;
}
