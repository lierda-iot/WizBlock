#include "lte_net_state_view.h"

#include <stddef.h>

static demo_net_detail_t map_fault_to_detail(network_manager_fault_code_t code)
{
    switch (code) {
        case NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED:
            return DEMO_NET_DETAIL_WIFI_AUTH_FAILED;
        case NETWORK_MANAGER_FAULT_WIFI_AP_NOT_FOUND:
            return DEMO_NET_DETAIL_WIFI_NOT_FOUND;
        case NETWORK_MANAGER_FAULT_WIFI_DISCONNECTED:
        case NETWORK_MANAGER_FAULT_WIFI_IP_LOST:
            return DEMO_NET_DETAIL_WIFI_DISCONNECTED;
        case NETWORK_MANAGER_FAULT_INTERNET_UNREACHABLE:
            return DEMO_NET_DETAIL_WIFI_NO_INTERNET;
        case NETWORK_MANAGER_FAULT_WIFI_CONFIG_WRITE_FAILED:
            return DEMO_NET_DETAIL_CONFIG_SAVE_FAILED;
        case NETWORK_MANAGER_FAULT_LTE_INIT_FAILED:
        case NETWORK_MANAGER_FAULT_LTE_POWER_ON_FAILED:
            return DEMO_NET_DETAIL_LTE_INIT_FAILED;
        case NETWORK_MANAGER_FAULT_MANAGER_INIT_FAILED:
        case NETWORK_MANAGER_FAULT_NETIF_INIT_FAILED:
        case NETWORK_MANAGER_FAULT_EVENT_LOOP_INIT_FAILED:
            return DEMO_NET_DETAIL_NETWORK_INIT_FAILED;
        default:
            return DEMO_NET_DETAIL_NONE;
    }
}

demo_net_state_t lte_net_state_view_map(
    const network_manager_snapshot_t *snapshot,
    bool no_network_timeout,
    demo_net_detail_t *detail)
{
    if (NULL == snapshot || NULL == detail) {
        return DEMO_NET_STATE_ERROR;
    }
    *detail = DEMO_NET_DETAIL_NONE;

    if (NETWORK_MANAGER_LIFECYCLE_START_FAILED == snapshot->lifecycle) {
        *detail = map_fault_to_detail(snapshot->active_fault);
        return DEMO_NET_STATE_ERROR;
    }
    if (NETWORK_MANAGER_LIFECYCLE_STARTING == snapshot->lifecycle) {
        return DEMO_NET_STATE_STARTING;
    }
    if (no_network_timeout && !snapshot->stable_ready) {
        return DEMO_NET_STATE_NO_NETWORK;
    }

    if (snapshot->stable_ready &&
        NETWORK_MANAGER_INTERFACE_4G ==
            snapshot->stable_active_interface) {
        if (!snapshot->wifi.raw_link_up ||
            !snapshot->wifi.raw_ipv4_ready) {
            return DEMO_NET_STATE_4G_CONNECTED;
        }
        *detail = DEMO_NET_DETAIL_4G_REMAINS_ONLINE;
        return DEMO_NET_STATE_SWITCHING_TO_WIFI;
    }

    if (NETWORK_MANAGER_WIFI_CONNECTING == snapshot->wifi_phase) {
        if (snapshot->cellular.enabled &&
            snapshot->cellular.raw_ipv4_ready) {
            *detail = DEMO_NET_DETAIL_4G_REMAINS_ONLINE;
        }
        return DEMO_NET_STATE_WIFI_CONNECTING;
    }
    if (NETWORK_MANAGER_WIFI_WAIT_STABLE == snapshot->wifi_phase) {
        if (snapshot->cellular.enabled &&
            snapshot->cellular.raw_ipv4_ready) {
            *detail = DEMO_NET_DETAIL_4G_REMAINS_ONLINE;
        }
        return DEMO_NET_STATE_WIFI_CHECKING;
    }
    if (NETWORK_MANAGER_WIFI_READY == snapshot->wifi_phase &&
        snapshot->interface_switch_in_progress) {
        if (snapshot->cellular.enabled &&
            snapshot->cellular.raw_ipv4_ready) {
            *detail = DEMO_NET_DETAIL_4G_REMAINS_ONLINE;
        }
        return DEMO_NET_STATE_SWITCHING_TO_WIFI;
    }
    if (snapshot->stable_ready) {
        if (NETWORK_MANAGER_INTERFACE_WIFI ==
            snapshot->stable_active_interface) {
            return DEMO_NET_STATE_WIFI_CONNECTED;
        }
        if (NETWORK_MANAGER_INTERFACE_4G ==
            snapshot->stable_active_interface) {
            return DEMO_NET_STATE_4G_CONNECTED;
        }
    }
    if (snapshot->cellular.enabled &&
        (NETWORK_MANAGER_4G_WAIT_LINK == snapshot->cellular_phase ||
         NETWORK_MANAGER_4G_WAIT_IPV4 == snapshot->cellular_phase ||
         NETWORK_MANAGER_4G_WAIT_INTERNET == snapshot->cellular_phase)) {
        if (NETWORK_MANAGER_WIFI_FAULT == snapshot->wifi_phase ||
            NETWORK_MANAGER_WIFI_EXHAUSTED == snapshot->wifi_phase) {
            *detail = map_fault_to_detail(snapshot->wifi.last_raw_reason);
        }
        return DEMO_NET_STATE_4G_CONNECTING;
    }
    if (snapshot->wifi.enabled) {
        return DEMO_NET_STATE_WIFI_CONNECTING;
    }
    if (snapshot->cellular.enabled) {
        return DEMO_NET_STATE_4G_CONNECTING;
    }
    return DEMO_NET_STATE_STARTING;
}

bool lte_net_state_view_is_immediate(demo_net_state_t state)
{
    return DEMO_NET_STATE_WIFI_CONNECTED != state;
}
