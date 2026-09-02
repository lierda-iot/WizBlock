#include "network_manager_dual_runtime_model.h"

int main(void)
{
    const network_manager_dual_runtime_input_t input = {
        .mode = NETWORK_MANAGER_MODE_DUAL_AUTO,
        .wifi_config_present = true,
        .wifi_raw_link_up = true,
        .wifi_raw_ipv4_ready = true,
        .wifi_stable_ready = true,
        .cellular_raw_link_up = false,
        .cellular_raw_ipv4_ready = false,
        .cellular_stable_ready = false,
        .internet_reachable = true,
        .raw_active_interface = NETWORK_MANAGER_INTERFACE_WIFI,
    };
    network_manager_dual_runtime_output_t output;

    network_manager_dual_runtime_model_reduce(&input, &output);
    if (NETWORK_MANAGER_INTERFACE_WIFI != output.stable_active_interface ||
        !output.raw_ready || !output.stable_ready ||
        output.all_retry_exhausted || output.interface_switch_in_progress) {
        return 1;
    }

    const network_manager_dual_runtime_input_t cellular_fallback = {
        .mode = NETWORK_MANAGER_MODE_DUAL_AUTO,
        .wifi_config_present = true,
        .wifi_raw_link_up = false,
        .wifi_raw_ipv4_ready = false,
        .wifi_stable_ready = false,
        .wifi_retry_exhausted = false,
        .cellular_raw_link_up = true,
        .cellular_raw_ipv4_ready = true,
        .cellular_stable_ready = true,
        .internet_reachable = true,
        .raw_active_interface = NETWORK_MANAGER_INTERFACE_4G,
    };
    network_manager_dual_runtime_model_reduce(&cellular_fallback, &output);
    if (NETWORK_MANAGER_INTERFACE_4G != output.stable_active_interface ||
        !output.raw_ready || !output.stable_ready ||
        output.all_retry_exhausted || output.interface_switch_in_progress) {
        return 2;
    }

    const network_manager_dual_runtime_input_t exhausted = {
        .mode = NETWORK_MANAGER_MODE_DUAL_AUTO,
        .wifi_config_present = false,
        .wifi_raw_link_up = false,
        .wifi_raw_ipv4_ready = false,
        .wifi_stable_ready = false,
        .wifi_retry_exhausted = false,
        .cellular_raw_link_up = false,
        .cellular_raw_ipv4_ready = false,
        .cellular_stable_ready = false,
        .raw_active_interface = NETWORK_MANAGER_INTERFACE_NONE,
    };
    network_manager_dual_runtime_model_reduce(&exhausted, &output);
    if (!output.all_retry_exhausted || output.raw_ready ||
        output.stable_ready ||
        NETWORK_MANAGER_INTERFACE_NONE != output.stable_active_interface) {
        return 3;
    }

    const network_manager_dual_runtime_input_t manual_offline = {
        .mode = NETWORK_MANAGER_MODE_DUAL_AUTO,
        .manual_offline = true,
        .wifi_config_present = true,
        .wifi_raw_link_up = true,
        .wifi_raw_ipv4_ready = true,
        .wifi_stable_ready = true,
        .cellular_raw_link_up = false,
        .cellular_raw_ipv4_ready = false,
        .cellular_stable_ready = false,
        .internet_reachable = true,
        .raw_active_interface = NETWORK_MANAGER_INTERFACE_WIFI,
    };
    network_manager_dual_runtime_model_reduce(&manual_offline, &output);
    if (!output.raw_ready || output.stable_ready ||
        output.all_retry_exhausted || !output.interface_switch_in_progress ||
        NETWORK_MANAGER_INTERFACE_NONE != output.stable_active_interface ||
        NETWORK_MANAGER_INTERFACE_WIFI != output.raw_active_interface) {
        return 4;
    }

    const network_manager_dual_runtime_input_t candidate_controls_active = {
        .mode = NETWORK_MANAGER_MODE_DUAL_AUTO,
        .wifi_config_present = true,
        .wifi_raw_link_up = true,
        .wifi_raw_ipv4_ready = true,
        .wifi_stable_ready = true,
        .cellular_raw_link_up = true,
        .cellular_raw_ipv4_ready = true,
        .cellular_stable_ready = true,
        .internet_reachable = true,
        .raw_active_interface = NETWORK_MANAGER_INTERFACE_4G,
        .previous_stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI,
    };
    network_manager_dual_runtime_model_reduce(
        &candidate_controls_active, &output);
    if (NETWORK_MANAGER_INTERFACE_4G != output.stable_active_interface ||
        !output.raw_ready || !output.stable_ready ||
        output.interface_switch_in_progress) {
        return 5;
    }

    /* A stale lsd active-interface fact must not hold onto a lost Wi-Fi path. */
    const network_manager_dual_runtime_input_t wifi_lost_cellular_ready = {
        .mode = NETWORK_MANAGER_MODE_DUAL_AUTO,
        .wifi_config_present = true,
        .wifi_raw_link_up = false,
        .wifi_raw_ipv4_ready = false,
        .wifi_stable_ready = true,
        .cellular_raw_link_up = true,
        .cellular_raw_ipv4_ready = true,
        .cellular_stable_ready = true,
        .internet_reachable = true,
        .raw_active_interface = NETWORK_MANAGER_INTERFACE_WIFI,
        .previous_stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI,
    };
    network_manager_dual_runtime_model_reduce(
        &wifi_lost_cellular_ready, &output);
    if (NETWORK_MANAGER_INTERFACE_4G != output.stable_active_interface ||
        !output.raw_ready || !output.stable_ready ||
        !output.interface_switch_in_progress) {
        return 7;
    }

    const network_manager_dual_runtime_input_t no_internet = {
        .mode = NETWORK_MANAGER_MODE_WIFI_ONLY,
        .wifi_config_present = true,
        .wifi_raw_link_up = true,
        .wifi_raw_ipv4_ready = true,
        .wifi_stable_ready = true,
        .internet_reachable = false,
        .raw_active_interface = NETWORK_MANAGER_INTERFACE_WIFI,
        .previous_stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI,
    };
    network_manager_dual_runtime_model_reduce(&no_internet, &output);
    if (output.raw_ready || output.stable_ready ||
        NETWORK_MANAGER_INTERFACE_NONE != output.stable_active_interface ||
        !output.interface_switch_in_progress ||
        output.all_retry_exhausted) {
        return 8;
    }
    return 0;
}
