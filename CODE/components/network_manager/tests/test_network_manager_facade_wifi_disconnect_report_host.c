#include "network_manager.h"
#include "fake_host.h"

bool network_manager_host_test_process_one(void);
void network_manager_host_test_process_runtime(uint32_t now_ms);

/* Covers the automatic raw-disconnect reporting path: when Wi-Fi drops on its
   own (WIFI_EVENT_STA_DISCONNECTED, not a manual request), network_manager must
   immediately send NET_WIFI_EVENT_DISCONNECTED to lsd_net_mgmt so the
   closed-source manager can switch the default route to 4G without delay.
   The manual/policy disconnect path is already exercised by
   test_network_manager_facade_host.c; this test guards the automatic path. */
int main(void)
{
    const network_manager_wifi_config_t valid = {
        .ssid = {'t', 'e', 's', 't'},
        .password = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'},
        .ssid_len = 4U,
        .password_len = 8U,
    };
    uint32_t operation_id = 0U;
    network_manager_snapshot_t snapshot;

    if (ESP_OK != network_manager_set_mode(NETWORK_MANAGER_MODE_DUAL_AUTO) ||
        ESP_OK != network_manager_start() ||
        ESP_OK != network_manager_wifi_set_config(
            &valid, false, &operation_id) ||
        !network_manager_host_test_process_one()) {
        return 1;
    }

    /* Drive Wi-Fi to stable ready so the manager has reported CONNECTED once. */
    fake_network_set(LSD_IF_WIFI, true);
    fake_esp_emit_event(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
    network_manager_host_test_process_runtime(0U);
    network_manager_host_test_process_runtime(10000U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.stable_ready ||
        NETWORK_MANAGER_WIFI_READY != snapshot.wifi_phase) {
        return 2;
    }
    if (1U != fake_net_event_count(NET_WIFI_EVENT_CONNECTED) ||
        0U != fake_net_event_count(NET_WIFI_EVENT_DISCONNECTED)) {
        return 3;
    }

    /* Automatic raw disconnect (never manual_offline): must report exactly one
       DISCONNECTED to lsd_net_mgmt and drop raw availability immediately. */
    wifi_event_sta_disconnected_t disconnected = {
        .reason = 42U,
    };
    fake_esp_emit_event(WIFI_EVENT,
                        WIFI_EVENT_STA_DISCONNECTED,
                        &disconnected);
    network_manager_host_test_process_runtime(20000U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        snapshot.wifi.raw_link_up || snapshot.wifi.raw_ipv4_ready) {
        return 4;
    }
    if (1U != fake_net_event_count(NET_WIFI_EVENT_DISCONNECTED)) {
        return 5;
    }
    /* The disconnect must never be misfiled as a spurious CONNECTED report. */
    if (1U != fake_net_event_count(NET_WIFI_EVENT_CONNECTED)) {
        return 6;
    }
    return 0;
}
