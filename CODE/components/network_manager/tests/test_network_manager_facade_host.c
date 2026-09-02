#include "network_manager.h"
#include "fake_host.h"

bool network_manager_host_test_process_one(void);
void network_manager_host_test_process_runtime(uint32_t now_ms);

static void on_event(const network_manager_event_t *event, void *user_ctx)
{
    (void)event;
    (void)user_ctx;
}

int main(void)
{
    network_manager_mode_t mode;
    if (ESP_OK != network_manager_get_mode(&mode) ||
        NETWORK_MANAGER_MODE_4G_ONLY != mode) {
        return 1;
    }
    if (ESP_OK != network_manager_set_mode(NETWORK_MANAGER_MODE_DUAL_AUTO) ||
        ESP_OK != network_manager_get_mode(&mode) ||
        NETWORK_MANAGER_MODE_DUAL_AUTO != mode) {
        return 2;
    }
    if (ESP_ERR_INVALID_ARG != network_manager_set_mode((network_manager_mode_t)9)) {
        return 3;
    }

    uint32_t subscription_id = 0U;
    if (ESP_OK != network_manager_subscribe(on_event, NULL, &subscription_id) ||
        0U == subscription_id) {
        return 4;
    }
    if (ESP_OK != network_manager_start()) {
        return 5;
    }
    if (ESP_ERR_INVALID_STATE !=
        network_manager_set_mode(NETWORK_MANAGER_MODE_WIFI_ONLY)) {
        return 6;
    }

    network_manager_wifi_config_t invalid = {0};
    uint32_t operation_id = 0U;
    if (ESP_ERR_INVALID_ARG != network_manager_wifi_set_config(
            &invalid, false, &operation_id)) {
        return 7;
    }
    network_manager_wifi_config_t valid = {
        .ssid = {'t', 'e', 's', 't'},
        .password = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'},
        .ssid_len = 4U,
        .password_len = 8U,
    };
    if (ESP_OK != network_manager_wifi_set_config(
            &valid, false, &operation_id) || 0U == operation_id) {
        return 8;
    }
    if (!network_manager_host_test_process_one()) {
        return 9;
    }
    network_manager_wifi_config_t current;
    network_manager_snapshot_t snapshot;
    if (ESP_OK != network_manager_wifi_get_current_config(&current) ||
        current.ssid_len != valid.ssid_len ||
        current.password_len != valid.password_len) {
        return 10;
    }
    fake_network_set(LSD_IF_WIFI, true);
    fake_esp_emit_event(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
    network_manager_host_test_process_runtime(0U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.raw_ready || snapshot.stable_ready ||
        NETWORK_MANAGER_WIFI_WAIT_STABLE != snapshot.wifi_phase) {
        return 11;
    }
    network_manager_host_test_process_runtime(10000U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.raw_ready || !snapshot.stable_ready ||
        NETWORK_MANAGER_INTERFACE_WIFI !=
            snapshot.stable_active_interface ||
        NETWORK_MANAGER_WIFI_READY != snapshot.wifi_phase) {
        return 12;
    }
    /* WiFi stability window completed: exactly one CONNECTED report to
       lsd_net_mgmt so the closed-source manager can prefer WiFi. */
    if (1U != fake_net_event_count(NET_WIFI_EVENT_CONNECTED)) {
        return 30;
    }
    uint32_t disconnect_id = 0U;
    uint32_t repeated_disconnect_id = 0U;
    uint32_t reconnect_id = 0U;
    if (ESP_OK != network_manager_request_disconnect(&disconnect_id) ||
        0U == disconnect_id ||
        ESP_OK != network_manager_request_disconnect(
            &repeated_disconnect_id) ||
        0U == repeated_disconnect_id ||
        disconnect_id == repeated_disconnect_id ||
        ESP_ERR_INVALID_STATE !=
            network_manager_request_reconnect(&reconnect_id) ||
        0U != reconnect_id) {
        return 13;
    }
    if (!network_manager_host_test_process_one()) {
        return 14;
    }
    if (1U != fake_net_event_count(NET_WIFI_EVENT_DISCONNECTED) ||
        1U != fake_wifi_disconnect_count() ||
        1U != fake_lte_power_off_count()) {
        return 20;
    }
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.manual_offline || snapshot.disconnect_in_progress ||
        snapshot.stable_ready) {
        return 15;
    }
    uint32_t no_action_disconnect_id = 0U;
    if (ESP_OK != network_manager_request_disconnect(
            &no_action_disconnect_id) ||
        !network_manager_host_test_process_one() ||
        1U != fake_wifi_disconnect_count() ||
        1U != fake_lte_power_off_count()) {
        return 23;
    }
    if (ESP_OK != network_manager_request_reconnect(&reconnect_id) ||
        0U == reconnect_id ||
        !network_manager_host_test_process_one()) {
        return 16;
    }
    if (37U != fake_last_task_delay_ticks()) {
        return 24;
    }
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        NETWORK_MANAGER_LIFECYCLE_RUNNING != snapshot.lifecycle ||
        NETWORK_MANAGER_MODE_DUAL_AUTO != snapshot.mode ||
        snapshot.manual_offline || !snapshot.reconnect_in_progress ||
        NETWORK_MANAGER_4G_WAIT_LINK != snapshot.cellular_phase) {
        return 17;
    }
    wifi_event_sta_disconnected_t disconnected = {
        .reason = 42U,
    };
    fake_esp_emit_event(WIFI_EVENT,
                        WIFI_EVENT_STA_DISCONNECTED,
                        &disconnected);
    network_manager_host_test_process_runtime(20000U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        1U != snapshot.wifi.retry_attempt ||
        NETWORK_MANAGER_WIFI_BACKOFF != snapshot.wifi_phase ||
        42 != snapshot.wifi.last_raw_reason) {
        return 18;
    }
    network_manager_fault_history_t history;
    bool initial_timeout_found = false;
    bool wifi_disconnect_found = false;
    if (ESP_OK != network_manager_get_fault_history(&history)) {
        return 21;
    }
    for (size_t index = 0U; index < history.count; ++index) {
        if (NETWORK_MANAGER_FAULT_CELLULAR_INITIAL_IPV4_TIMEOUT ==
            history.records[index].code) {
            initial_timeout_found = true;
        }
        if (NETWORK_MANAGER_FAULT_WIFI_DISCONNECTED ==
                history.records[index].code &&
            42 == history.records[index].raw_reason) {
            wifi_disconnect_found = true;
        }
    }
    if (!initial_timeout_found || !wifi_disconnect_found) {
        return 22;
    }
    disconnected.reason = WIFI_REASON_AUTH_FAIL;
    fake_esp_emit_event(WIFI_EVENT,
                        WIFI_EVENT_STA_DISCONNECTED,
                        &disconnected);
    network_manager_host_test_process_runtime(20001U);
    bool auth_fault_found = false;
    if (ESP_OK != network_manager_get_fault_history(&history)) {
        return 24;
    }
    for (size_t index = 0U; index < history.count; ++index) {
        if (NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED ==
                history.records[index].code &&
            WIFI_REASON_AUTH_FAIL == history.records[index].raw_reason) {
            auth_fault_found = true;
            break;
        }
    }
    if (!auth_fault_found) {
        return 25;
    }
    if (ESP_OK != network_manager_unsubscribe(subscription_id) ||
        ESP_ERR_NOT_FOUND != network_manager_unsubscribe(subscription_id)) {
        return 19;
    }
    return 0;
}
