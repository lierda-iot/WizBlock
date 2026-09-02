#include "network_manager_event_journal.h"

int main(void)
{
    network_manager_fault_history_t history;
    network_manager_fault_record_t recorded;

    network_manager_fault_history_init(&history);
    if (0U != history.count || 1U != history.next_sequence ||
        0U != history.overwritten_count) {
        return 1;
    }
    if (!network_manager_fault_history_record(
            &history,
            NETWORK_MANAGER_INTERFACE_WIFI,
            NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED,
            ESP_FAIL,
            17,
            &recorded)) {
        return 2;
    }
    if (1U != recorded.sequence || 1U != recorded.occurrence_count ||
        !recorded.active || 1U != history.count ||
        2U != history.next_sequence) {
        return 3;
    }
    if (!network_manager_fault_history_record(
            &history,
            NETWORK_MANAGER_INTERFACE_WIFI,
            NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED,
            ESP_FAIL,
            18,
            &recorded)) {
        return 4;
    }
    if (2U != recorded.sequence || 2U != recorded.occurrence_count ||
        history.records[0].active || !history.records[1].active) {
        return 5;
    }
    if (NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED !=
        network_manager_fault_history_latest_active(&history)) {
        return 6;
    }
    if (!network_manager_fault_history_mark_inactive(
            &history,
            NETWORK_MANAGER_INTERFACE_WIFI,
            NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED,
            &recorded)) {
        return 7;
    }
    if (recorded.active ||
        NETWORK_MANAGER_FAULT_NONE !=
            network_manager_fault_history_latest_active(&history)) {
        return 8;
    }

    network_manager_fault_history_init(&history);
    for (uint32_t index = 0U;
         index < NETWORK_MANAGER_FAULT_HISTORY_CAPACITY + 2U;
         ++index) {
        if (!network_manager_fault_history_record(
                &history,
                NETWORK_MANAGER_INTERFACE_4G,
                NETWORK_MANAGER_FAULT_CELLULAR_IP_LOST,
                ESP_FAIL,
                (int32_t)index,
                &recorded)) {
            return 9;
        }
    }
    if (NETWORK_MANAGER_FAULT_HISTORY_CAPACITY != history.count ||
        2U != history.overwritten_count ||
        3U != history.records[0].sequence ||
        18U != history.records[15].sequence ||
        18U != history.records[15].occurrence_count ||
        19U != history.next_sequence) {
        return 10;
    }

    network_manager_fault_history_init(&history);
    history.next_sequence = UINT32_MAX;
    if (!network_manager_fault_history_record(
            &history,
            NETWORK_MANAGER_INTERFACE_WIFI,
            NETWORK_MANAGER_FAULT_WIFI_IP_LOST,
            ESP_FAIL,
            0,
            &recorded) ||
        UINT32_MAX != recorded.sequence || 1U != history.next_sequence) {
        return 11;
    }
    if (!network_manager_fault_history_record(
            &history,
            NETWORK_MANAGER_INTERFACE_4G,
            NETWORK_MANAGER_FAULT_CELLULAR_IP_LOST,
            ESP_FAIL,
            0,
            &recorded) ||
        1U != recorded.sequence || 2U != history.next_sequence) {
        return 12;
    }
    return 0;
}
