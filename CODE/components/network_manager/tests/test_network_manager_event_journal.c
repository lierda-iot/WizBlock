#include "network_manager_event_journal.h"

#include <stdint.h>

int main(void)
{
    if (1U != network_manager_next_nonzero_sequence(0U)) {
        return 1;
    }
    if (2U != network_manager_next_nonzero_sequence(1U)) {
        return 2;
    }
    if (1U != network_manager_next_nonzero_sequence(UINT32_MAX)) {
        return 3;
    }

    static network_manager_event_journal_t journal;
    static const network_manager_event_t first = {
        .type = NETWORK_MANAGER_EVENT_READY_CHANGED,
        .snapshot = {
            .revision = 10U,
        },
    };
    static const network_manager_event_t second = {
        .type = NETWORK_MANAGER_EVENT_ACTIVE_INTERFACE_CHANGED,
        .snapshot = {
            .revision = 11U,
        },
    };
    static network_manager_event_t taken;

    network_manager_event_journal_init(&journal);
    if (!network_manager_event_journal_publish(&journal, &first, false) ||
        !network_manager_event_journal_publish(&journal, &second, false)) {
        return 4;
    }
    if (!network_manager_event_journal_take_next(&journal, &taken) ||
        1U != taken.sequence ||
        NETWORK_MANAGER_EVENT_READY_CHANGED != taken.type ||
        10U != taken.snapshot.revision ||
        1U != taken.snapshot.event_sequence) {
        return 5;
    }
    if (!network_manager_event_journal_take_next(&journal, &taken) ||
        2U != taken.sequence ||
        NETWORK_MANAGER_EVENT_ACTIVE_INTERFACE_CHANGED != taken.type ||
        2U != taken.snapshot.event_sequence) {
        return 6;
    }
    if (network_manager_event_journal_take_next(&journal, &taken)) {
        return 7;
    }

    network_manager_event_journal_init(&journal);
    static const network_manager_event_t normal_before = {
        .type = NETWORK_MANAGER_EVENT_READY_CHANGED,
    };
    static const network_manager_event_t critical_fault = {
        .type = NETWORK_MANAGER_EVENT_FAULT,
    };
    static const network_manager_event_t normal_after = {
        .type = NETWORK_MANAGER_EVENT_ACTIVE_INTERFACE_CHANGED,
    };
    if (!network_manager_event_journal_publish(
            &journal, &normal_before, false) ||
        !network_manager_event_journal_publish(
            &journal, &critical_fault, true) ||
        !network_manager_event_journal_publish(
            &journal, &normal_after, false)) {
        return 8;
    }
    if (!network_manager_event_journal_take_critical(&journal, &taken) ||
        NETWORK_MANAGER_EVENT_FAULT != taken.type || 2U != taken.sequence) {
        return 9;
    }
    if (!network_manager_event_journal_take_next(&journal, &taken) ||
        1U != taken.sequence) {
        return 10;
    }
    if (!network_manager_event_journal_take_next(&journal, &taken) ||
        3U != taken.sequence) {
        return 11;
    }
    if (network_manager_event_journal_take_next(&journal, &taken) ||
        network_manager_event_journal_take_critical(&journal, &taken)) {
        return 12;
    }

    network_manager_event_journal_init(&journal);
    static const network_manager_event_t overflowing = {
        .type = NETWORK_MANAGER_EVENT_RAW_STATE_CHANGED,
    };
    for (size_t index = 0U;
         index < NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY + 2U;
         ++index) {
        if (!network_manager_event_journal_publish(
                &journal, &overflowing, false)) {
            return 13;
        }
    }
    if (2U != journal.overflow_count ||
        NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY != journal.count) {
        return 14;
    }
    if (!network_manager_event_journal_take_critical(&journal, &taken) ||
        NETWORK_MANAGER_EVENT_STREAM_OVERFLOW != taken.type ||
        2U != taken.snapshot.event_overflow_count) {
        return 15;
    }
    if (!network_manager_event_journal_take_next(&journal, &taken) ||
        3U != taken.sequence) {
        return 16;
    }
    return 0;
}
