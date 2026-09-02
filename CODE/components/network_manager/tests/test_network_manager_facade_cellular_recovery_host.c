/*
 * Passive cellular recovery test (2026-08-12).
 *
 * Verifies that after 4G disconnect, the system transitions to WAIT_LINK
 * passively without power cycling or generating recovery timeout faults.
 */
#include "network_manager.h"
#include "fake_host.h"

void network_manager_host_test_process_runtime(uint32_t now_ms);

int main(void)
{
    network_manager_snapshot_t snapshot;
    network_manager_fault_history_t history;

    if (ESP_OK != network_manager_start()) {
        return 1;
    }
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        NETWORK_MANAGER_LIFECYCLE_STARTING != snapshot.lifecycle) {
        return 2;
    }

    /* Establish 4G connection. */
    fake_network_set(LSD_IF_4G, true);
    fake_esp_emit_event(ETH_EVENT, ETHERNET_EVENT_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_ETH_GOT_IP, NULL);
    network_manager_host_test_process_runtime(0U);

    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        NETWORK_MANAGER_4G_READY != snapshot.cellular_phase) {
        return 3;
    }
    if (0U != snapshot.cellular.retry_attempt ||
        0U != snapshot.cellular.retry_limit ||
        snapshot.cellular.retry_exhausted ||
        snapshot.all_retry_exhausted) {
        return 4;
    }

    /* The closed-source 4G module reports disconnect through this event. */
    if (ESP_OK != fake_lsd_emit_event(NET_4G_EVENT_DISCONNECTED)) {
        return 5;
    }
    network_manager_host_test_process_runtime(100U);

    if (ESP_OK != network_manager_get_snapshot(&snapshot)) {
        return 6;
    }

    /* The event must clear the stale 4G facts in one worker iteration. */
    if (NETWORK_MANAGER_4G_WAIT_LINK != snapshot.cellular_phase ||
        snapshot.cellular.raw_link_up ||
        snapshot.cellular.raw_ipv4_ready ||
        snapshot.internet_reachable ||
        snapshot.raw_ready ||
        snapshot.stable_ready ||
        NETWORK_MANAGER_INTERFACE_NONE != snapshot.raw_active_interface ||
        NETWORK_MANAGER_INTERFACE_NONE != snapshot.stable_active_interface) {
        return 7;
    }

    /* Observation must not produce any automatic recovery effect. */
    if (snapshot.cellular.retry_exhausted) {
        return 8;
    }
    if (0U != snapshot.cellular.retry_attempt ||
        0U != snapshot.cellular.retry_limit ||
        snapshot.all_retry_exhausted) {
        return 9;
    }
    if (0U != fake_lte_power_off_count()) {
        return 10;
    }
    if (0U != fake_lsd_netif_query_count() ||
        0U != fake_lsd_ready_query_count() ||
        ESP_OK != network_manager_get_fault_history(&history)) {
        return 11;
    }

    const uint32_t revision = snapshot.revision;
    const size_t fault_count = history.count;
    fake_esp_emit_event(IP_EVENT, IP_EVENT_ETH_LOST_IP, NULL);
    network_manager_host_test_process_runtime(200U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        ESP_OK != network_manager_get_fault_history(&history) ||
        revision != snapshot.revision ||
        fault_count != history.count ||
        0U != fake_lte_power_off_count()) {
        return 12;
    }

    return 0;
}
