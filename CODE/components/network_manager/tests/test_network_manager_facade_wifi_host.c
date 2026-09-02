#include "network_manager.h"

bool network_manager_host_test_process_one(void);

int main(void)
{
    uint32_t operation_id = 0U;
    network_manager_snapshot_t snapshot;
    network_manager_fault_history_t history;
    network_manager_wifi_config_t config;
    bool config_fault_found = false;

    if (ESP_ERR_INVALID_STATE !=
            network_manager_wifi_get_current_config(&config) ||
        ESP_ERR_INVALID_STATE !=
            network_manager_wifi_get_persisted_config(&config) ||
        ESP_ERR_INVALID_STATE !=
            network_manager_wifi_clear_persisted_config(&operation_id)) {
        return 5;
    }
    if (ESP_OK != network_manager_set_mode(NETWORK_MANAGER_MODE_WIFI_ONLY) ||
        ESP_OK != network_manager_start() ||
        ESP_OK != network_manager_request_reconnect(&operation_id) ||
        0U == operation_id ||
        !network_manager_host_test_process_one() ||
        ESP_OK != network_manager_get_snapshot(&snapshot)) {
        return 1;
    }
    if (snapshot.reconnect_in_progress) {
        return 2;
    }
    if (ESP_OK != network_manager_get_fault_history(&history)) {
        return 3;
    }
    for (size_t index = 0U; index < history.count; ++index) {
        if (NETWORK_MANAGER_FAULT_WIFI_CONFIG_MISSING ==
            history.records[index].code) {
            config_fault_found = true;
            break;
        }
    }
    return config_fault_found ? 0 : 4;
}
