#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTIFIER_MAX_TASKS             12U
#define NOTIFIER_TASKS_PER_PAGE        4U
#define NOTIFIER_MAX_EVENTS            32U
#define NOTIFIER_TASK_ID_MAX_BYTES     96U
#define NOTIFIER_PROJECT_MAX_BYTES     48U
#define NOTIFIER_TASK_TITLE_MAX_BYTES  72U
#define NOTIFIER_OFFLINE_FAILURES      3U
#define NOTIFIER_PAGE_INTERVAL_MS      4000U
#define NOTIFIER_HIGHLIGHT_DURATION_MS 3000U
#define NOTIFIER_ALERT_BATCH_MS        2000U
#define NOTIFIER_ALERT_ANIMATION_MS    2500U
#define NOTIFIER_EVENT_MAX_AGE_MS      60000U

typedef enum {
    NOTIFIER_SURFACE_APP = 0,
    NOTIFIER_SURFACE_VS,
    NOTIFIER_SURFACE_CODEX,
} notifier_surface_t;

typedef enum {
    NOTIFIER_TASK_RUN = 0,
    NOTIFIER_TASK_DONE,
    NOTIFIER_TASK_STOP,
    NOTIFIER_TASK_UNKNOWN,
} notifier_task_status_t;

typedef enum {
    NOTIFIER_EVENT_TURN_COMPLETED = 0,
    NOTIFIER_EVENT_TURN_STOPPED,
} notifier_event_type_t;

typedef enum {
    NOTIFIER_AGGREGATE_IDLE = 0,
    NOTIFIER_AGGREGATE_RUNNING,
} notifier_aggregate_state_t;

typedef struct {
    char id[NOTIFIER_TASK_ID_MAX_BYTES + 1U];
    notifier_surface_t surface;
    char project[NOTIFIER_PROJECT_MAX_BYTES + 1U];
    char title[NOTIFIER_TASK_TITLE_MAX_BYTES + 1U];
    notifier_task_status_t status;
    uint32_t elapsed_ms;
    uint64_t updated_at_ms;
} notifier_task_t;

typedef struct {
    uint64_t seq;
    char task_id[NOTIFIER_TASK_ID_MAX_BYTES + 1U];
    notifier_event_type_t type;
    bool notify;
    uint64_t occurred_at_ms;
} notifier_event_t;

typedef struct {
    uint8_t schema_version;
    uint64_t generated_at_ms;
    notifier_aggregate_state_t aggregate_state;
    uint16_t total_count;
    uint16_t running_count;
    uint16_t done_count;
    uint16_t stop_count;
    uint16_t overflow_count;
    uint8_t task_count;
    notifier_task_t tasks[NOTIFIER_MAX_TASKS];
    uint8_t event_count;
    notifier_event_t events[NOTIFIER_MAX_EVENTS];
    bool events_truncated;
} notifier_snapshot_t;

typedef struct {
    bool cursor_changed;
    uint64_t last_event_seq;
    uint8_t notify_event_count;
    uint8_t skipped_event_count;
} notifier_commit_result_t;

typedef struct {
    uint8_t page_index;
    uint8_t page_count;
    uint8_t start_index;
    uint8_t row_count;
    char highlight_task_id[NOTIFIER_TASK_ID_MAX_BYTES + 1U];
} notifier_page_view_t;

typedef struct {
    notifier_snapshot_t snapshot;
    bool has_snapshot;
    bool online;
    uint8_t consecutive_failures;
    uint64_t last_event_seq;
    uint32_t revision;
    char highlight_task_id[NOTIFIER_TASK_ID_MAX_BYTES + 1U];
    uint64_t highlight_until_ms;
    bool alert_pending;
    uint64_t alert_due_ms;
    uint32_t alert_generation;
    uint64_t alert_animation_started_ms;
    uint64_t alert_animation_until_ms;
    char alert_title[NOTIFIER_TASK_TITLE_MAX_BYTES + 1U];
} notifier_model_t;

void notifier_model_init(notifier_model_t *model, uint64_t persisted_event_seq);
notifier_commit_result_t notifier_model_commit(notifier_model_t *model,
                                                const notifier_snapshot_t *snapshot,
                                                uint64_t monotonic_ms);
void notifier_model_record_poll_failure(notifier_model_t *model);
bool notifier_model_is_online(const notifier_model_t *model);
uint8_t notifier_model_consecutive_failures(const notifier_model_t *model);
bool notifier_model_alert_pending(const notifier_model_t *model);
bool notifier_model_take_alert(notifier_model_t *model, uint64_t monotonic_ms);
void notifier_model_get_page(const notifier_model_t *model,
                             uint64_t monotonic_ms,
                             notifier_page_view_t *page);

const char *notifier_surface_name(notifier_surface_t surface);
const char *notifier_task_status_name(notifier_task_status_t status);
