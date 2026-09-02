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
    network_manager_fault_history_t history;
    bool exhausted_fault_found = false;

    if (ESP_OK != network_manager_set_mode(NETWORK_MANAGER_MODE_WIFI_ONLY) ||
        ESP_OK != network_manager_start() ||
        ESP_OK != network_manager_wifi_set_config(
            &config, false, &operation_id) ||
        !network_manager_host_test_process_one()) {
        return 1;
    }

    uint32_t now_ms = 0U;
    for (uint8_t failure = 0U; failure < 11U; ++failure) {
        wifi_event_sta_disconnected_t disconnected = {
            .reason = (int)(failure + 1U),
        };
        fake_esp_emit_event(WIFI_EVENT,
                            WIFI_EVENT_STA_DISCONNECTED,
                            &disconnected);
        network_manager_host_test_process_runtime(now_ms);
        if (failure < 10U) {
            now_ms += 30000U;
            network_manager_host_test_process_runtime(now_ms);
        }
    }

    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.wifi.retry_exhausted ||
        !snapshot.all_retry_exhausted ||
        NETWORK_MANAGER_WIFI_EXHAUSTED != snapshot.wifi_phase) {
        return 2;
    }
    if (ESP_OK != network_manager_get_fault_history(&history)) {
        return 3;
    }
    for (size_t index = 0U; index < history.count; ++index) {
        if (NETWORK_MANAGER_FAULT_WIFI_RETRY_EXHAUSTED ==
            history.records[index].code) {
            exhausted_fault_found = true;
            break;
        }
    }
    if (!exhausted_fault_found) {
        return 4;
    }
    if (ESP_OK != network_manager_request_reconnect(&operation_id) ||
        !network_manager_host_test_process_one()) {
        return 5;
    }
    now_ms = 0U;
    for (uint8_t failure = 0U; failure < 11U; ++failure) {
        wifi_event_sta_disconnected_t disconnected = {
            .reason = (int)(failure + 1U),
        };
        fake_esp_emit_event(WIFI_EVENT,
                            WIFI_EVENT_STA_DISCONNECTED,
                            &disconnected);
        network_manager_host_test_process_runtime(now_ms);
        if (failure < 10U) {
            now_ms += 30000U;
            network_manager_host_test_process_runtime(now_ms);
        }
    }
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        snapshot.reconnect_in_progress) {
        return 6;
    }
    if (ESP_OK != network_manager_request_reconnect(&operation_id) ||
        !network_manager_host_test_process_one()) {
        return 7;
    }
    fake_network_set(LSD_IF_WIFI, true);
    fake_esp_emit_event(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
    network_manager_host_test_process_runtime(0U);
    network_manager_host_test_process_runtime(10000U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        !snapshot.stable_ready || snapshot.reconnect_in_progress ||
        ESP_OK != network_manager_get_fault_history(&history)) {
        return 8;
    }
    for (size_t index = history.count; index > 0U; --index) {
        if ((NETWORK_MANAGER_FAULT_WIFI_RETRY_EXHAUSTED ==
                 history.records[index - 1U].code ||
             NETWORK_MANAGER_FAULT_ALL_RETRY_EXHAUSTED ==
                 history.records[index - 1U].code) &&
            history.records[index - 1U].active) {
            return 9;
        }
    }
    return 0;
}
