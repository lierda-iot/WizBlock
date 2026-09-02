#include "notifier_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static notifier_snapshot_t make_snapshot(uint8_t task_count)
{
    notifier_snapshot_t snapshot = {0};

    assert(task_count <= NOTIFIER_MAX_TASKS);
    snapshot.schema_version = 1U;
    snapshot.generated_at_ms = 100000U;
    snapshot.aggregate_state = NOTIFIER_AGGREGATE_RUNNING;
    snapshot.total_count = task_count;
    snapshot.running_count = (0U < task_count) ? task_count - 1U : 0U;
    snapshot.done_count = (0U < task_count) ? 1U : 0U;
    snapshot.task_count = task_count;
    for (uint8_t index = 0U; index < snapshot.task_count; ++index) {
        snprintf(snapshot.tasks[index].id, sizeof(snapshot.tasks[index].id),
                 "task-%u", (unsigned)index);
        snprintf(snapshot.tasks[index].project, sizeof(snapshot.tasks[index].project),
                 "project-%u", (unsigned)index);
        snprintf(snapshot.tasks[index].title, sizeof(snapshot.tasks[index].title),
                 "task title %u", (unsigned)index);
        snapshot.tasks[index].surface = NOTIFIER_SURFACE_VS;
        snapshot.tasks[index].status =
            (task_count - 1U == index) ? NOTIFIER_TASK_DONE : NOTIFIER_TASK_RUN;
        snapshot.tasks[index].elapsed_ms = (uint32_t)index * 1000U;
        snapshot.tasks[index].updated_at_ms = 90000U + index;
    }
    return snapshot;
}

static void test_three_failures_enter_offline_and_success_recovers(void)
{
    notifier_model_t model = {0};
    notifier_snapshot_t snapshot = make_snapshot(9U);

    notifier_model_init(&model, 0U);
    assert(!notifier_model_is_online(&model));
    notifier_model_commit(&model, &snapshot, 1000U);
    assert(notifier_model_is_online(&model));

    notifier_model_record_poll_failure(&model);
    notifier_model_record_poll_failure(&model);
    assert(notifier_model_is_online(&model));
    notifier_model_record_poll_failure(&model);
    assert(!notifier_model_is_online(&model));
    assert(3U == notifier_model_consecutive_failures(&model));

    notifier_model_commit(&model, &snapshot, 2000U);
    assert(notifier_model_is_online(&model));
    assert(0U == notifier_model_consecutive_failures(&model));
}

static void test_fresh_events_advance_cursor_highlight_and_merge_alert(void)
{
    notifier_model_t model = {0};
    notifier_snapshot_t snapshot = make_snapshot(9U);
    notifier_commit_result_t result = {0};
    notifier_page_view_t page = {0};

    snapshot.event_count = 2U;
    snapshot.events[0].seq = 10U;
    strcpy(snapshot.events[0].task_id, "task-8");
    snapshot.events[0].type = NOTIFIER_EVENT_TURN_COMPLETED;
    snapshot.events[0].notify = true;
    snapshot.events[0].occurred_at_ms = 99000U;
    snapshot.events[1] = snapshot.events[0];
    snapshot.events[1].seq = 11U;

    notifier_model_init(&model, 9U);
    result = notifier_model_commit(&model, &snapshot, 1000U);

    assert(result.cursor_changed);
    assert(11U == result.last_event_seq);
    assert(2U == result.notify_event_count);
    assert(notifier_model_alert_pending(&model));
    assert(!notifier_model_take_alert(&model, 2999U));
    assert(notifier_model_take_alert(&model, 3000U));
    assert(1U == model.alert_generation);
    assert(3000U == model.alert_animation_started_ms);
    assert(3000U + NOTIFIER_ALERT_ANIMATION_MS ==
           model.alert_animation_until_ms);
    assert(0 == strcmp(model.alert_title, "task title 8"));
    assert(!notifier_model_take_alert(&model, 3001U));

    notifier_model_get_page(&model, 1500U, &page);
    assert(2U == page.page_index);
    assert(3U == page.page_count);
    assert(8U == page.start_index);
    assert(1U == page.row_count);
    assert(0 == strcmp(page.highlight_task_id, "task-8"));

    result = notifier_model_commit(&model, &snapshot, 4000U);
    assert(!result.cursor_changed);
    assert(0U == result.notify_event_count);
    assert(!notifier_model_alert_pending(&model));
}

static void test_old_future_stopped_and_notify_false_events_do_not_alert(void)
{
    notifier_model_t model = {0};
    notifier_snapshot_t snapshot = make_snapshot(9U);
    notifier_commit_result_t result = {0};

    snapshot.event_count = 4U;
    for (uint8_t index = 0U; index < snapshot.event_count; ++index) {
        snapshot.events[index].seq = (uint64_t)index + 1U;
        strcpy(snapshot.events[index].task_id, "task-8");
        snapshot.events[index].type = NOTIFIER_EVENT_TURN_COMPLETED;
        snapshot.events[index].notify = true;
        snapshot.events[index].occurred_at_ms = 100000U;
    }
    snapshot.events[0].occurred_at_ms = 39999U;
    snapshot.events[1].occurred_at_ms = 100001U;
    snapshot.events[2].type = NOTIFIER_EVENT_TURN_STOPPED;
    snapshot.events[3].notify = false;

    notifier_model_init(&model, 0U);
    result = notifier_model_commit(&model, &snapshot, 1000U);

    assert(4U == result.last_event_seq);
    assert(0U == result.notify_event_count);
    assert(4U == result.skipped_event_count);
    assert(!notifier_model_alert_pending(&model));
}

static void test_auto_page_changes_every_four_seconds_after_highlight_expires(void)
{
    notifier_model_t model = {0};
    notifier_snapshot_t snapshot = make_snapshot(9U);
    notifier_page_view_t page = {0};

    notifier_model_init(&model, 0U);
    notifier_model_commit(&model, &snapshot, 0U);

    notifier_model_get_page(&model, 3999U, &page);
    assert(0U == page.page_index);
    notifier_model_get_page(&model, 4000U, &page);
    assert(1U == page.page_index);
    notifier_model_get_page(&model, 8000U, &page);
    assert(2U == page.page_index);
    notifier_model_get_page(&model, 12000U, &page);
    assert(0U == page.page_index);
}

static void test_page_count_boundaries_use_four_cards_per_page(void)
{
    static const uint8_t task_counts[] = {4U, 5U, 8U, 9U, 12U};
    static const uint8_t expected_pages[] = {1U, 2U, 2U, 3U, 3U};

    for (size_t index = 0U; index < sizeof(task_counts); ++index) {
        notifier_model_t model = {0};
        notifier_snapshot_t snapshot = make_snapshot(task_counts[index]);
        notifier_page_view_t page = {0};

        notifier_model_init(&model, 0U);
        notifier_model_commit(&model, &snapshot, 0U);
        notifier_model_get_page(&model, 0U, &page);

        assert(expected_pages[index] == page.page_count);
        assert(0U == page.page_index);
        assert(0U == page.start_index);
        assert((task_counts[index] < 4U ? task_counts[index] : 4U) ==
               page.row_count);
    }
}

int main(void)
{
    test_three_failures_enter_offline_and_success_recovers();
    test_fresh_events_advance_cursor_highlight_and_merge_alert();
    test_old_future_stopped_and_notify_false_events_do_not_alert();
    test_auto_page_changes_every_four_seconds_after_highlight_expires();
    test_page_count_boundaries_use_four_cards_per_page();
    puts("notifier_model_test: PASS");
    return 0;
}
