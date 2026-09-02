#include "network_manager.h"
#include "fake_host.h"

int main(void)
{
    network_manager_snapshot_t snapshot;
    uint32_t operation_id = 0U;

    fake_set_task_create_fail_on_call(2U);
    if (ESP_ERR_NO_MEM != network_manager_start() ||
        ESP_OK != network_manager_get_snapshot(&snapshot) ||
        NETWORK_MANAGER_LIFECYCLE_START_FAILED != snapshot.lifecycle) {
        return 1;
    }
    if (1U != fake_task_delete_count() ||
        1U != fake_queue_delete_count() ||
        3U != fake_event_unregister_count()) {
        return 2;
    }
    if (ESP_ERR_INVALID_STATE != network_manager_start() ||
        ESP_ERR_INVALID_STATE !=
            network_manager_request_disconnect(&operation_id)) {
        return 3;
    }
    return 0;
}
