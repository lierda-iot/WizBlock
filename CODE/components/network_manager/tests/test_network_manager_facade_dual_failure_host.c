#include "network_manager.h"
#include "fake_host.h"

bool network_manager_host_test_process_one(void);

int main(void)
{
    network_manager_snapshot_t snapshot;
    network_manager_fault_history_t history;
    uint32_t operation_id = 0U;
    bool power_fault_found = false;

    if (ESP_OK != network_manager_set_mode(NETWORK_MANAGER_MODE_DUAL_AUTO) ||
        ESP_OK != network_manager_start()) {
        return 1;
    }
    fake_network_set(LSD_IF_WIFI, true);
    fake_set_lte_power_off_result(ESP_FAIL);
    if (ESP_OK != network_manager_request_disconnect(&operation_id) ||
        !network_manager_host_test_process_one() ||
        1U != fake_wifi_disconnect_count() ||
        1U != fake_lte_power_off_count() ||
        ESP_OK != network_manager_get_snapshot(&snapshot)) {
        return 2;
    }
    if (!snapshot.manual_offline || snapshot.disconnect_in_progress ||
        snapshot.stable_ready) {
        return 3;
    }
    if (ESP_OK != network_manager_get_fault_history(&history)) {
        return 4;
    }
    for (size_t index = 0U; index < history.count; ++index) {
        if (NETWORK_MANAGER_FAULT_LTE_POWER_OFF_FAILED ==
            history.records[index].code) {
            power_fault_found = true;
            break;
        }
    }
    return power_fault_found ? 0 : 5;
}
