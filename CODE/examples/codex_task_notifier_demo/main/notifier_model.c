#include "notifier_model.h"

#include <limits.h>
#include <string.h>

static uint64_t add_saturated_u64(uint64_t value, uint64_t increment)
{
    if (UINT64_MAX - value < increment) {
        return UINT64_MAX;
    }
    return value + increment;
}

static const notifier_task_t *find_task(const notifier_snapshot_t *snapshot,
                                        const char *task_id)
{
    if (NULL == snapshot || NULL == task_id) {
        return NULL;
    }
    for (uint8_t index = 0U; index < snapshot->task_count; ++index) {
        if (0 == strcmp(snapshot->tasks[index].id, task_id)) {
            return &snapshot->tasks[index];
        }
    }
    return NULL;
}

void notifier_model_init(notifier_model_t *model, uint64_t persisted_event_seq)
{
    if (NULL == model) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->last_event_seq = persisted_event_seq;
}

notifier_commit_result_t notifier_model_commit(notifier_model_t *model,
                                                const notifier_snapshot_t *snapshot,
                                                uint64_t monotonic_ms)
{
    notifier_commit_result_t result = {0};

    if (NULL == model || NULL == snapshot) {
        return result;
    }

    for (uint8_t index = 0U; index < snapshot->event_count; ++index) {
        const notifier_event_t *event = &snapshot->events[index];
        const notifier_task_t *task = NULL;
        bool eligible = false;

        if (event->seq <= model->last_event_seq) {
            continue;
        }
        model->last_event_seq = event->seq;
        result.cursor_changed = true;

        task = find_task(snapshot, event->task_id);
        if (NOTIFIER_EVENT_TURN_COMPLETED == event->type && NULL != task) {
            memcpy(model->highlight_task_id, event->task_id,
                   sizeof(model->highlight_task_id));
            model->highlight_until_ms = add_saturated_u64(
                monotonic_ms, NOTIFIER_HIGHLIGHT_DURATION_MS);
        }

        eligible = event->notify &&
                   (NOTIFIER_EVENT_TURN_COMPLETED == event->type) &&
                   (snapshot->generated_at_ms >= event->occurred_at_ms) &&
                   ((snapshot->generated_at_ms - event->occurred_at_ms) <=
                    NOTIFIER_EVENT_MAX_AGE_MS);
        if (eligible) {
            if (NULL != task) {
                memcpy(model->alert_title, task->title,
                       sizeof(model->alert_title));
            } else {
                static const char fallback_title[] = "Task complete";

                memset(model->alert_title, 0, sizeof(model->alert_title));
                memcpy(model->alert_title, fallback_title,
                       sizeof(fallback_title));
            }
            if (!model->alert_pending) {
                model->alert_pending = true;
                model->alert_due_ms = add_saturated_u64(monotonic_ms,
                                                        NOTIFIER_ALERT_BATCH_MS);
            }
            if (UINT8_MAX != result.notify_event_count) {
                result.notify_event_count++;
            }
        } else if (UINT8_MAX != result.skipped_event_count) {
            result.skipped_event_count++;
        }
    }

    model->snapshot = *snapshot;
    model->has_snapshot = true;
    model->online = true;
    model->consecutive_failures = 0U;
    if (UINT32_MAX != model->revision) {
        model->revision++;
    }
    result.last_event_seq = model->last_event_seq;
    return result;
}

void notifier_model_record_poll_failure(notifier_model_t *model)
{
    bool was_online = false;

    if (NULL == model) {
        return;
    }
    was_online = model->online;
    if (UINT8_MAX != model->consecutive_failures) {
        model->consecutive_failures++;
    }
    if (NOTIFIER_OFFLINE_FAILURES <= model->consecutive_failures) {
        model->online = false;
    }
    if (was_online != model->online && UINT32_MAX != model->revision) {
        model->revision++;
    }
}

bool notifier_model_is_online(const notifier_model_t *model)
{
    return (NULL != model) && model->online;
}

uint8_t notifier_model_consecutive_failures(const notifier_model_t *model)
{
    return (NULL != model) ? model->consecutive_failures : UINT8_MAX;
}

bool notifier_model_alert_pending(const notifier_model_t *model)
{
    return (NULL != model) && model->alert_pending;
}

bool notifier_model_take_alert(notifier_model_t *model, uint64_t monotonic_ms)
{
    if (NULL == model || !model->alert_pending || monotonic_ms < model->alert_due_ms) {
        return false;
    }
    model->alert_pending = false;
    model->alert_animation_started_ms = monotonic_ms;
    model->alert_animation_until_ms = add_saturated_u64(
        monotonic_ms, NOTIFIER_ALERT_ANIMATION_MS);
    if (UINT32_MAX != model->alert_generation) {
        model->alert_generation++;
    }
    if (UINT32_MAX != model->revision) {
        model->revision++;
    }
    return true;
}

void notifier_model_get_page(const notifier_model_t *model,
                             uint64_t monotonic_ms,
                             notifier_page_view_t *page)
{
    uint8_t page_index = 0U;

    if (NULL == page) {
        return;
    }
    memset(page, 0, sizeof(*page));
    page->page_count = 1U;
    if (NULL == model || !model->has_snapshot) {
        return;
    }

    if (NOTIFIER_TASKS_PER_PAGE < model->snapshot.task_count) {
        page->page_count = (uint8_t)(
            (model->snapshot.task_count + NOTIFIER_TASKS_PER_PAGE - 1U) /
            NOTIFIER_TASKS_PER_PAGE);
        if (monotonic_ms < model->highlight_until_ms &&
            '\0' != model->highlight_task_id[0]) {
            for (uint8_t index = 0U; index < model->snapshot.task_count; ++index) {
                if (0 == strcmp(model->snapshot.tasks[index].id,
                                model->highlight_task_id)) {
                    page_index = (uint8_t)(index / NOTIFIER_TASKS_PER_PAGE);
                    break;
                }
            }
        } else {
            page_index = (uint8_t)(
                (monotonic_ms / NOTIFIER_PAGE_INTERVAL_MS) % page->page_count);
        }
    }

    page->page_index = page_index;
    page->start_index = (uint8_t)(page_index * NOTIFIER_TASKS_PER_PAGE);
    if (page->start_index < model->snapshot.task_count) {
        uint8_t remaining = (uint8_t)(model->snapshot.task_count - page->start_index);
        page->row_count = (remaining < NOTIFIER_TASKS_PER_PAGE)
                              ? remaining
                              : NOTIFIER_TASKS_PER_PAGE;
    }
    if (monotonic_ms < model->highlight_until_ms &&
        '\0' != model->highlight_task_id[0]) {
        memcpy(page->highlight_task_id, model->highlight_task_id,
               sizeof(page->highlight_task_id));
    }
}

const char *notifier_surface_name(notifier_surface_t surface)
{
    switch (surface) {
        case NOTIFIER_SURFACE_APP:
            return "Desktop";
        case NOTIFIER_SURFACE_VS:
            return "VSCode";
        case NOTIFIER_SURFACE_CODEX:
            return "Codex";
        default:
            return "Codex";
    }
}

const char *notifier_task_status_name(notifier_task_status_t status)
{
    switch (status) {
        case NOTIFIER_TASK_RUN:
            return "RUN";
        case NOTIFIER_TASK_DONE:
            return "DONE";
        case NOTIFIER_TASK_STOP:
            return "STOP";
        case NOTIFIER_TASK_UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNKNOWN";
    }
}
