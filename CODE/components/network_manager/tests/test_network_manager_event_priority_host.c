#include "network_manager.h"

void network_manager_host_test_dispatch_all(void);
bool network_manager_host_test_process_one(void);

static network_manager_event_type_t s_types[8];
static unsigned int s_count;

static void on_event(const network_manager_event_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (s_count < 8U) {
        s_types[s_count] = event->type;
        ++s_count;
    }
}

int main(void)
{
    uint32_t subscription_id = 0U;
    bool start_seen = false;
    if (ESP_OK != network_manager_subscribe(
            on_event, NULL, &subscription_id) ||
        ESP_OK != network_manager_start()) {
        return 1;
    }
    (void)network_manager_host_test_process_one();
    network_manager_host_test_dispatch_all();
    /* The fake worker times out during start, so its critical fault is first. */
    if (0U == s_count ||
        NETWORK_MANAGER_EVENT_FAULT != s_types[0]) {
        return 2;
    }
    bool snapshot_seen = false;
    for (unsigned int index = 0U; index < s_count; ++index) {
        if (NETWORK_MANAGER_EVENT_SNAPSHOT_SYNC == s_types[index]) {
            snapshot_seen = true;
        }
        if (NETWORK_MANAGER_EVENT_START_RESULT == s_types[index]) {
            start_seen = true;
        }
    }
    if (!snapshot_seen) {
        return 3;
    }
    return start_seen ? 0 : 4;
}
