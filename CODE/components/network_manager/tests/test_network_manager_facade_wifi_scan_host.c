#include "network_manager.h"

bool network_manager_host_test_process_one(void);
void network_manager_host_test_process_runtime(uint32_t now_ms);

int main(void)
{
    uint32_t operation_id = 0U;
    uint32_t duplicate_id = 0U;
    network_manager_snapshot_t snapshot = {0};
    network_manager_wifi_scan_list_t list = {0};

    if (ESP_ERR_INVALID_STATE !=
        network_manager_wifi_scan_start(&operation_id)) {
        return 1;
    }
    if (ESP_OK != network_manager_set_mode(NETWORK_MANAGER_MODE_WIFI_ONLY) ||
        ESP_OK != network_manager_start() ||
        ESP_OK != network_manager_wifi_scan_start(&operation_id) ||
        0U == operation_id ||
        ESP_ERR_INVALID_STATE !=
            network_manager_wifi_scan_start(&duplicate_id) ||
        !network_manager_host_test_process_one() ||
        ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.wifi_scan_in_progress) {
        return 2;
    }
    network_manager_host_test_process_runtime(1U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        snapshot.wifi_scan_in_progress ||
        0U == snapshot.wifi_scan_revision ||
        ESP_OK != network_manager_wifi_scan_get_latest(&list) ||
        list.operation_id != operation_id ||
        list.revision != snapshot.wifi_scan_revision ||
        ESP_OK != list.result || 0U != list.count) {
        return 3;
    }
    return 0;
}
