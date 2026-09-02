#include "network_manager.h"
#include "fake_host.h"

bool network_manager_host_test_process_one(void);
void network_manager_host_test_process_runtime(uint32_t now_ms);

int main(void)
{
    const network_manager_wifi_config_t config = {
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
            &config, false, &operation_id) ||
        !network_manager_host_test_process_one()) {
        return 1;
    }

    fake_network_set(LSD_IF_WIFI, true);
    fake_esp_emit_event(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
    fake_esp_emit_event(ETH_EVENT, ETHERNET_EVENT_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_ETH_GOT_IP, NULL);
    network_manager_host_test_process_runtime(0U);
    network_manager_host_test_process_runtime(10000U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.stable_ready ||
        NETWORK_MANAGER_INTERFACE_WIFI !=
            snapshot.stable_active_interface) {
        return 2;
    }

    if (ESP_OK != fake_lsd_emit_event(NET_4G_EVENT_DISCONNECTED)) {
        return 3;
    }
    network_manager_host_test_process_runtime(10100U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.wifi.raw_link_up ||
        !snapshot.wifi.raw_ipv4_ready ||
        snapshot.cellular.raw_link_up ||
        snapshot.cellular.raw_ipv4_ready ||
        !snapshot.internet_reachable ||
        !snapshot.raw_ready ||
        !snapshot.stable_ready ||
        NETWORK_MANAGER_INTERFACE_WIFI != snapshot.raw_active_interface ||
        NETWORK_MANAGER_INTERFACE_WIFI !=
            snapshot.stable_active_interface ||
        0U != fake_lte_power_off_count()) {
        return 4;
    }

    return 0;
}
