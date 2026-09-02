#include "lte_net_state_view.h"

#include <assert.h>
#include <stdio.h>

static network_manager_snapshot_t dual_snapshot(void)
{
    return (network_manager_snapshot_t){
        .lifecycle = NETWORK_MANAGER_LIFECYCLE_RUNNING,
        .mode = NETWORK_MANAGER_MODE_DUAL_AUTO,
        .wifi = {.enabled = true},
        .cellular = {.enabled = true, .raw_ipv4_ready = true},
    };
}

static void test_wifi_loss_displays_4g_immediately(void)
{
    network_manager_snapshot_t snapshot = dual_snapshot();
    snapshot.stable_ready = true;
    snapshot.stable_active_interface = NETWORK_MANAGER_INTERFACE_4G;
    snapshot.wifi_phase = NETWORK_MANAGER_WIFI_BACKOFF;
    demo_net_detail_t detail = DEMO_NET_DETAIL_NONE;

    assert(DEMO_NET_STATE_4G_CONNECTED ==
           lte_net_state_view_map(&snapshot, false, &detail));
    assert(DEMO_NET_DETAIL_NONE == detail);
    assert(lte_net_state_view_is_immediate(DEMO_NET_STATE_4G_CONNECTED));
}

static void test_wifi_recovery_keeps_4g_and_shows_switching(void)
{
    network_manager_snapshot_t snapshot = dual_snapshot();
    snapshot.stable_ready = true;
    snapshot.stable_active_interface = NETWORK_MANAGER_INTERFACE_4G;
    snapshot.wifi.raw_link_up = true;
    snapshot.wifi.raw_ipv4_ready = true;
    snapshot.wifi_phase = NETWORK_MANAGER_WIFI_WAIT_STABLE;
    demo_net_detail_t detail = DEMO_NET_DETAIL_NONE;

    assert(DEMO_NET_STATE_SWITCHING_TO_WIFI ==
           lte_net_state_view_map(&snapshot, false, &detail));
    assert(DEMO_NET_DETAIL_4G_REMAINS_ONLINE == detail);
    assert(lte_net_state_view_is_immediate(
        DEMO_NET_STATE_SWITCHING_TO_WIFI));
}

static void test_wifi_connected_retains_short_visual_debounce(void)
{
    network_manager_snapshot_t snapshot = dual_snapshot();
    snapshot.stable_ready = true;
    snapshot.stable_active_interface = NETWORK_MANAGER_INTERFACE_WIFI;
    snapshot.wifi.raw_link_up = true;
    snapshot.wifi.raw_ipv4_ready = true;
    snapshot.wifi_phase = NETWORK_MANAGER_WIFI_READY;
    demo_net_detail_t detail = DEMO_NET_DETAIL_NONE;

    assert(DEMO_NET_STATE_WIFI_CONNECTED ==
           lte_net_state_view_map(&snapshot, false, &detail));
    assert(!lte_net_state_view_is_immediate(DEMO_NET_STATE_WIFI_CONNECTED));
}

static void test_wifi_ready_waiting_for_route_keeps_switching(void)
{
    network_manager_snapshot_t snapshot = dual_snapshot();
    snapshot.stable_ready = true;
    snapshot.stable_active_interface = NETWORK_MANAGER_INTERFACE_4G;
    snapshot.raw_active_interface = NETWORK_MANAGER_INTERFACE_4G;
    snapshot.wifi.raw_link_up = true;
    snapshot.wifi.raw_ipv4_ready = true;
    snapshot.wifi_phase = NETWORK_MANAGER_WIFI_READY;
    snapshot.interface_switch_in_progress = false;
    demo_net_detail_t detail = DEMO_NET_DETAIL_NONE;

    assert(DEMO_NET_STATE_SWITCHING_TO_WIFI ==
           lte_net_state_view_map(&snapshot, false, &detail));
    assert(DEMO_NET_DETAIL_4G_REMAINS_ONLINE == detail);
}

int main(void)
{
    test_wifi_loss_displays_4g_immediately();
    test_wifi_recovery_keeps_4g_and_shows_switching();
    test_wifi_connected_retains_short_visual_debounce();
    test_wifi_ready_waiting_for_route_keeps_switching();
    puts("lte_net_state_view_test: PASS");
    return 0;
}
