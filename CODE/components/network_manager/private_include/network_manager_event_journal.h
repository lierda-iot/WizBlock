#pragma once

/** Bounded FIFO events, critical retention, and fault-history primitives. */

#include "network_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY 32U
#define NETWORK_MANAGER_EVENT_TYPE_COUNT       18U

typedef struct {
    network_manager_event_t events[NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY];
    bool dispatched_out_of_band[NETWORK_MANAGER_EVENT_JOURNAL_CAPACITY];
    network_manager_event_t critical_events[NETWORK_MANAGER_EVENT_TYPE_COUNT];
    uint64_t critical_order[NETWORK_MANAGER_EVENT_TYPE_COUNT];
    size_t head;
    size_t count;
    uint32_t last_sequence;
    uint32_t overflow_count;
    uint32_t critical_pending;
    uint64_t publish_order;
} network_manager_event_journal_t;

uint32_t network_manager_next_nonzero_sequence(uint32_t current);
void network_manager_event_journal_init(
    network_manager_event_journal_t *journal);
/** Publish a copied event; critical events also use out-of-band retention. */
bool network_manager_event_journal_publish(
    network_manager_event_journal_t *journal,
    const network_manager_event_t *event,
    bool critical);
/** Take the next ordinary FIFO event that was not dispatched out of band. */
bool network_manager_event_journal_take_next(
    network_manager_event_journal_t *journal,
    network_manager_event_t *event);
/** Take the oldest pending critical event by publication order. */
bool network_manager_event_journal_take_critical(
    network_manager_event_journal_t *journal,
    network_manager_event_t *event);

void network_manager_fault_history_init(
    network_manager_fault_history_t *history);
/** Append an active occurrence and return the exact stored record. */
bool network_manager_fault_history_record(
    network_manager_fault_history_t *history,
    network_manager_interface_t interface,
    network_manager_fault_code_t code,
    esp_err_t source_error,
    int32_t raw_reason,
    network_manager_fault_record_t *recorded);
/** Mark the newest matching active record recovered without deleting it. */
bool network_manager_fault_history_mark_inactive(
    network_manager_fault_history_t *history,
    network_manager_interface_t interface,
    network_manager_fault_code_t code,
    network_manager_fault_record_t *recorded);
/** Return the code of the newest active record, or FAULT_NONE. */
network_manager_fault_code_t network_manager_fault_history_latest_active(
    const network_manager_fault_history_t *history);
