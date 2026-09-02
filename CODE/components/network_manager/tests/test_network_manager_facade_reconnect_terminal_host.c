#include "network_manager.h"
#include "fake_host.h"

bool network_manager_host_test_process_one(void);
void network_manager_host_test_process_runtime(uint32_t now_ms);
void network_manager_host_test_dispatch_all(void);

typedef struct {
    unsigned int accepted_count;
    unsigned int completed_count;
    unsigned int failed_count;
} reconnect_events_t;

static void on_event(const network_manager_event_t *event, void *user_ctx)
{
    reconnect_events_t *events = (reconnect_events_t *)user_ctx;
    if (NETWORK_MANAGER_EVENT_RECONNECT_STATUS != event->type) {
        return;
    }
    if (NETWORK_MANAGER_OPERATION_ACCEPTED == event->operation_status) {
        ++events->accepted_count;
    } else if (NETWORK_MANAGER_OPERATION_COMPLETED ==
               event->operation_status) {
        ++events->completed_count;
    } else if (NETWORK_MANAGER_OPERATION_FAILED == event->operation_status) {
        ++events->failed_count;
    }
}

int main(void)
{
    const network_manager_wifi_config_t config = {
        .ssid = {'t', 'e', 's', 't'},
        .password = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'},
        .ssid_len = 4U,
        .password_len = 8U,
    };
    reconnect_events_t events = {0};
    uint32_t subscription_id = 0U;
    uint32_t operation_id = 0U;

    if (ESP_OK != network_manager_set_mode(NETWORK_MANAGER_MODE_DUAL_AUTO) ||
        ESP_OK != network_manager_subscribe(
            on_event, &events, &subscription_id) ||
        ESP_OK != network_manager_start()) {
        return 1;
    }
    network_manager_host_test_dispatch_all();
    if (ESP_OK != network_manager_wifi_set_config(
            &config, false, &operation_id) ||
        !network_manager_host_test_process_one()) {
        return 2;
    }
    network_manager_host_test_dispatch_all();

    fake_set_wifi_config_result(ESP_FAIL);
    if (ESP_OK != network_manager_request_reconnect(&operation_id) ||
        !network_manager_host_test_process_one()) {
        return 3;
    }
    network_manager_host_test_dispatch_all();
    fake_network_set(LSD_IF_4G, true);
    fake_esp_emit_event(ETH_EVENT, ETHERNET_EVENT_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_ETH_GOT_IP, NULL);
    network_manager_host_test_process_runtime(1U);
    network_manager_host_test_dispatch_all();

    if (1U != events.accepted_count ||
        1U != events.completed_count ||
        0U != events.failed_count) {
        return 4;
    }
    return 0;
}
