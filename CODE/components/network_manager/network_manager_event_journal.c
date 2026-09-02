/*
 * Bounded event delivery and fault retention.
 *
 * Ordinary events use FIFO storage. Critical event types also retain their
 * latest pending value out of band so queue pressure cannot silently erase the
 * latest critical fact.
 */
#include "network_manager_event_journal.h"

#include <stddef.h>
#include <stdint.h>

uint32_t network_manager_next_nonzero_sequence(uint32_t current)
{
    /* Zero is reserved as "not assigned", including after uint32_t wrap. */
    return UINT32_MAX == current ? 1U : current + 1U;
}

static void record_critical_event(
    network_manager_event_journal_t *journal,
    const network_manager_event_t *event)
{
    const int event_type = (int)event->type;

    if (0 > event_type ||
        NETWORK_MANAGER_EVENT_TYPE_COUNT <= (size_t)event_type) {
        return;
    }

    const size_t critical_index = (size_t)event_type;
    ++journal->publish_order;
    journal->critical_events[critical_index] = *event;
    journal->critical_order[critical_index] = journal->publish_order;
    journal->critical_pending |= 1UL << critical_index;
}

void network_manager_event_journal_init(
    network_manager_event_journal_t *journal)
{
    if (NULL == journal) {
        return;
    }
    unsigned char *bytes = (unsigned char *)journal;
    for (size_t index = 0U; index < sizeof(*journal); ++index) {
        bytes[index] = 0U;
    }
}

bool network_manager_event_journal_publish(
    network_manager_event_journal_t *journal,
    const network_manager_event_t *event,
    bool critical)
{
    if (NULL == journal || NULL == event) {
        return false;
    }
    if (NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY == journal->count) {
        /* Drop the oldest ordinary event and retain an overflow notification. */
        journal->head =
            (journal->head + 1U) % NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY;
        --journal->count;
        if (UINT32_MAX != journal->overflow_count) {
            ++journal->overflow_count;
        }

        network_manager_event_t overflow = *event;
        overflow.type = NETWORK_MANAGER_EVENT_STREAM_OVERFLOW;
        journal->last_sequence =
            network_manager_next_nonzero_sequence(journal->last_sequence);
        overflow.sequence = journal->last_sequence;
        overflow.snapshot.event_sequence = overflow.sequence;
        overflow.snapshot.event_overflow_count = journal->overflow_count;
        record_critical_event(journal, &overflow);
    }

    const size_t tail =
        (journal->head + journal->count) %
        NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY;
    network_manager_event_t stored = *event;
    journal->last_sequence =
        network_manager_next_nonzero_sequence(journal->last_sequence);
    stored.sequence = journal->last_sequence;
    stored.snapshot.event_sequence = stored.sequence;
    stored.snapshot.event_overflow_count = journal->overflow_count;
    journal->events[tail] = stored;
    journal->dispatched_out_of_band[tail] = false;
    ++journal->count;

    if (critical) {
        record_critical_event(journal, &stored);
    }
    return true;
}

bool network_manager_event_journal_take_next(
    network_manager_event_journal_t *journal,
    network_manager_event_t *event)
{
    if (NULL == journal || NULL == event) {
        return false;
    }

    while (0U != journal->count) {
        const size_t head = journal->head;
        journal->head =
            (journal->head + 1U) % NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY;
        --journal->count;
        if (journal->dispatched_out_of_band[head]) {
            journal->dispatched_out_of_band[head] = false;
            continue;
        }
        *event = journal->events[head];
        return true;
    }
    return false;
}

bool network_manager_event_journal_take_critical(
    network_manager_event_journal_t *journal,
    network_manager_event_t *event)
{
    if (NULL == journal || NULL == event ||
        0U == journal->critical_pending) {
        return false;
    }

    /* Select by publication order, not enum value, across critical types. */
    size_t selected = NETWORK_MANAGER_EVENT_TYPE_COUNT;
    uint64_t selected_order = UINT64_MAX;
    for (size_t index = 0U; index < NETWORK_MANAGER_EVENT_TYPE_COUNT; ++index) {
        const uint32_t bit = 1UL << index;
        if (0U != (journal->critical_pending & bit) &&
            journal->critical_order[index] < selected_order) {
            selected = index;
            selected_order = journal->critical_order[index];
        }
    }
    if (NETWORK_MANAGER_EVENT_TYPE_COUNT == selected) {
        return false;
    }

    *event = journal->critical_events[selected];
    journal->critical_pending &= ~(1UL << selected);
    /* Mark the FIFO copy consumed so it is not delivered a second time. */
    for (size_t offset = 0U; offset < journal->count; ++offset) {
        const size_t index =
            (journal->head + offset) % NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY;
        if (event->sequence == journal->events[index].sequence) {
            journal->dispatched_out_of_band[index] = true;
            break;
        }
    }
    return true;
}

void network_manager_fault_history_init(
    network_manager_fault_history_t *history)
{
    if (NULL == history) {
        return;
    }
    unsigned char *bytes = (unsigned char *)history;
    for (size_t index = 0U; index < sizeof(*history); ++index) {
        bytes[index] = 0U;
    }
    history->next_sequence = 1U;
}

bool network_manager_fault_history_record(
    network_manager_fault_history_t *history,
    network_manager_interface_t interface,
    network_manager_fault_code_t code,
    esp_err_t source_error,
    int32_t raw_reason,
    network_manager_fault_record_t *recorded)
{
    if (NULL == history || NULL == recorded ||
        NETWORK_MANAGER_FAULT_NONE == code) {
        return false;
    }

    /* Only the newest occurrence of a code/interface pair remains active. */
    uint32_t occurrence_count = 1U;
    for (size_t index = history->count; 0U != index; --index) {
        network_manager_fault_record_t *previous =
            &history->records[index - 1U];
        if (interface == previous->interface && code == previous->code) {
            occurrence_count = UINT32_MAX == previous->occurrence_count ?
                UINT32_MAX : previous->occurrence_count + 1U;
            previous->active = false;
            break;
        }
    }

    if (NETWORK_MANAGER_FAULT_HISTORY_CAPACITY == history->count) {
        /* Preserve oldest-to-newest query order while evicting one record. */
        for (size_t index = 1U; index < history->count; ++index) {
            history->records[index - 1U] = history->records[index];
        }
        --history->count;
        if (UINT32_MAX != history->overwritten_count) {
            ++history->overwritten_count;
        }
    }

    if (0U == history->next_sequence) {
        history->next_sequence = 1U;
    }
    network_manager_fault_record_t next = {
        .sequence = history->next_sequence,
        .occurrence_count = occurrence_count,
        .interface = interface,
        .code = code,
        .source_error = source_error,
        .raw_reason = raw_reason,
        .active = true,
    };
    history->next_sequence =
        network_manager_next_nonzero_sequence(history->next_sequence);
    history->records[history->count] = next;
    ++history->count;
    *recorded = next;
    return true;
}

bool network_manager_fault_history_mark_inactive(
    network_manager_fault_history_t *history,
    network_manager_interface_t interface,
    network_manager_fault_code_t code,
    network_manager_fault_record_t *recorded)
{
    if (NULL == history || NULL == recorded ||
        NETWORK_MANAGER_FAULT_NONE == code) {
        return false;
    }

    for (size_t index = history->count; 0U != index; --index) {
        network_manager_fault_record_t *candidate =
            &history->records[index - 1U];
        if (candidate->active && interface == candidate->interface &&
            code == candidate->code) {
            candidate->active = false;
            *recorded = *candidate;
            return true;
        }
    }
    return false;
}

network_manager_fault_code_t network_manager_fault_history_latest_active(
    const network_manager_fault_history_t *history)
{
    if (NULL == history) {
        return NETWORK_MANAGER_FAULT_NONE;
    }
    for (size_t index = history->count; 0U != index; --index) {
        if (history->records[index - 1U].active) {
            return history->records[index - 1U].code;
        }
    }
    return NETWORK_MANAGER_FAULT_NONE;
}
