#include "notifier_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *VALID_JSON =
    "{"
    "\"schema_version\":1,"
    "\"generated_at_ms\":1785286900000,"
    "\"bridge_status\":\"ONLINE\","
    "\"aggregate\":{"
      "\"state\":\"RUNNING\",\"total_count\":2,\"running_count\":1,"
      "\"done_count\":1,\"stop_count\":0,\"overflow_count\":0},"
    "\"tasks\":["
      "{\"id\":\"task-a\",\"surface\":\"APP\",\"project\":\"project-a\","
       "\"title\":\"Fix notifier\","
       "\"status\":\"RUN\",\"elapsed_ms\":1234,\"updated_at_ms\":1785286899000},"
      "{\"id\":\"task-b\",\"surface\":\"VS\",\"project\":\"\\u9879\\u76ee\","
       "\"title\":\"\\u4fee\\u590d Bridge\","
       "\"status\":\"DONE\",\"elapsed_ms\":30000,\"updated_at_ms\":1785286898000}"
    "],"
    "\"events\":[{\"seq\":18446744073709551615,\"task_id\":\"task-b\","
      "\"type\":\"TURN_COMPLETED\",\"notify\":true,"
      "\"occurred_at_ms\":1785286898000}],"
    "\"events_truncated\":false,"
    "\"future_field\":{\"ignored\":true}"
    "}";

static bool parse(const char *json, notifier_snapshot_t *snapshot)
{
    notifier_protocol_error_t error = NOTIFIER_PROTOCOL_OK;
    bool ok = notifier_protocol_parse(json, strlen(json), snapshot, &error);
    if (!ok) {
        fprintf(stderr, "parse failed: %s\n", notifier_protocol_error_name(error));
    }
    return ok;
}

static void assert_invalid(const char *json);

static void test_valid_contract_and_uint64_max(void)
{
    notifier_snapshot_t snapshot = {0};

    assert(parse(VALID_JSON, &snapshot));
    assert(1U == snapshot.schema_version);
    assert(NOTIFIER_AGGREGATE_RUNNING == snapshot.aggregate_state);
    assert(2U == snapshot.task_count);
    assert(0 == strcmp(snapshot.tasks[1].project, "项目"));
    assert(0 == strcmp(snapshot.tasks[1].title, "修复 Bridge"));
    assert(1U == snapshot.event_count);
    assert(UINT64_MAX == snapshot.events[0].seq);
    assert(snapshot.events[0].notify);
}

static void test_missing_title_falls_back_to_project(void)
{
    const char *json =
        "{\"schema_version\":1,\"generated_at_ms\":0,\"bridge_status\":\"ONLINE\","
        "\"aggregate\":{\"state\":\"RUNNING\",\"total_count\":1,\"running_count\":1,"
        "\"done_count\":0,\"stop_count\":0,\"overflow_count\":0},\"tasks\":["
        "{\"id\":\"x\",\"surface\":\"APP\",\"project\":\"legacy\",\"status\":\"RUN\","
        "\"elapsed_ms\":0,\"updated_at_ms\":0}],\"events\":[]}";
    notifier_snapshot_t snapshot = {0};

    assert(parse(json, &snapshot));
    assert(0 == strcmp(snapshot.tasks[0].title, "legacy"));
}

static void test_empty_and_oversized_titles_are_rejected(void)
{
    assert_invalid(
        "{\"schema_version\":1,\"generated_at_ms\":0,\"bridge_status\":\"ONLINE\","
        "\"aggregate\":{\"state\":\"RUNNING\",\"total_count\":1,\"running_count\":1,"
        "\"done_count\":0,\"stop_count\":0,\"overflow_count\":0},\"tasks\":["
        "{\"id\":\"x\",\"surface\":\"APP\",\"project\":\"p\",\"title\":\"\","
        "\"status\":\"RUN\",\"elapsed_ms\":0,\"updated_at_ms\":0}],\"events\":[]}");
    assert_invalid(
        "{\"schema_version\":1,\"generated_at_ms\":0,\"bridge_status\":\"ONLINE\","
        "\"aggregate\":{\"state\":\"RUNNING\",\"total_count\":1,\"running_count\":1,"
        "\"done_count\":0,\"stop_count\":0,\"overflow_count\":0},\"tasks\":["
        "{\"id\":\"x\",\"surface\":\"APP\",\"project\":\"p\","
        "\"title\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"status\":\"RUN\",\"elapsed_ms\":0,\"updated_at_ms\":0}],\"events\":[]}");
}

static void assert_invalid(const char *json)
{
    notifier_snapshot_t snapshot = {0};
    notifier_protocol_error_t error = NOTIFIER_PROTOCOL_OK;

    assert(!notifier_protocol_parse(json, strlen(json), &snapshot, &error));
    assert(NOTIFIER_PROTOCOL_OK != error);
}

static void test_missing_null_unknown_enum_and_count_mismatch_are_rejected(void)
{
    assert_invalid("{\"schema_version\":1}");
    assert_invalid("{\"schema_version\":null}");
    assert_invalid(
        "{\"schema_version\":1,\"generated_at_ms\":0,\"bridge_status\":\"ONLINE\","
        "\"aggregate\":{\"state\":\"IDLE\",\"total_count\":0,\"running_count\":0,"
        "\"done_count\":0,\"stop_count\":0,\"overflow_count\":0},"
        "\"tasks\":[{\"id\":\"x\",\"surface\":\"WEB\",\"project\":\"p\","
        "\"status\":\"RUN\",\"elapsed_ms\":0,\"updated_at_ms\":0}],\"events\":[]}");
    assert_invalid(
        "{\"schema_version\":1,\"generated_at_ms\":0,\"bridge_status\":\"ONLINE\","
        "\"aggregate\":{\"state\":\"IDLE\",\"total_count\":1,\"running_count\":0,"
        "\"done_count\":0,\"stop_count\":0,\"overflow_count\":0},"
        "\"tasks\":[],\"events\":[]}");
}

static void test_duplicate_task_ids_and_nonascending_events_are_rejected(void)
{
    const char *duplicate_tasks =
        "{\"schema_version\":1,\"generated_at_ms\":0,\"bridge_status\":\"ONLINE\","
        "\"aggregate\":{\"state\":\"RUNNING\",\"total_count\":2,\"running_count\":2,"
        "\"done_count\":0,\"stop_count\":0,\"overflow_count\":0},\"tasks\":["
        "{\"id\":\"same\",\"surface\":\"APP\",\"project\":\"a\",\"status\":\"RUN\","
        "\"elapsed_ms\":0,\"updated_at_ms\":0},"
        "{\"id\":\"same\",\"surface\":\"VS\",\"project\":\"b\",\"status\":\"RUN\","
        "\"elapsed_ms\":0,\"updated_at_ms\":0}],\"events\":[]}";
    const char *bad_events =
        "{\"schema_version\":1,\"generated_at_ms\":10,\"bridge_status\":\"ONLINE\","
        "\"aggregate\":{\"state\":\"IDLE\",\"total_count\":0,\"running_count\":0,"
        "\"done_count\":0,\"stop_count\":0,\"overflow_count\":0},\"tasks\":[],\"events\":["
        "{\"seq\":2,\"task_id\":\"x\",\"type\":\"TURN_COMPLETED\",\"notify\":true,"
        "\"occurred_at_ms\":9},{\"seq\":1,\"task_id\":\"y\","
        "\"type\":\"TURN_STOPPED\",\"notify\":false,\"occurred_at_ms\":9}]}";

    assert_invalid(duplicate_tasks);
    assert_invalid(bad_events);
}

int main(void)
{
    test_valid_contract_and_uint64_max();
    test_missing_title_falls_back_to_project();
    test_empty_and_oversized_titles_are_rejected();
    test_missing_null_unknown_enum_and_count_mismatch_are_rejected();
    test_duplicate_task_ids_and_nonascending_events_are_rejected();
    puts("notifier_protocol_test: PASS");
    return 0;
}
