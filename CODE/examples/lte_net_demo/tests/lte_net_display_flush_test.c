#include "lte_net_display_flush.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    int wait_result;
    int wait_count;
    int ready_count;
    int sequence[2];
    int sequence_count;
} flush_fixture_t;

static int wait_for_flush(void *context)
{
    flush_fixture_t *fixture = (flush_fixture_t *)context;
    fixture->wait_count++;
    fixture->sequence[fixture->sequence_count++] = 1;
    return fixture->wait_result;
}

static void mark_flush_ready(void *context)
{
    flush_fixture_t *fixture = (flush_fixture_t *)context;
    fixture->ready_count++;
    fixture->sequence[fixture->sequence_count++] = 2;
}

static void test_success_waits_before_ready(void)
{
    flush_fixture_t fixture = {0};
    int result = lte_net_display_flush_complete(
        0, wait_for_flush, mark_flush_ready, &fixture);

    assert(0 == result);
    assert(1 == fixture.wait_count);
    assert(1 == fixture.ready_count);
    assert(2 == fixture.sequence_count);
    assert(1 == fixture.sequence[0]);
    assert(2 == fixture.sequence[1]);
}

static void test_draw_failure_skips_wait_and_releases_once(void)
{
    flush_fixture_t fixture = {0};
    int result = lte_net_display_flush_complete(
        -17, wait_for_flush, mark_flush_ready, &fixture);

    assert(-17 == result);
    assert(0 == fixture.wait_count);
    assert(1 == fixture.ready_count);
    assert(1 == fixture.sequence_count);
    assert(2 == fixture.sequence[0]);
}

static void test_wait_failure_is_returned_and_releases_once(void)
{
    flush_fixture_t fixture = {.wait_result = -23};
    int result = lte_net_display_flush_complete(
        0, wait_for_flush, mark_flush_ready, &fixture);

    assert(-23 == result);
    assert(1 == fixture.wait_count);
    assert(1 == fixture.ready_count);
}

int main(void)
{
    test_success_waits_before_ready();
    test_draw_failure_skips_wait_and_releases_once();
    test_wait_failure_is_returned_and_releases_once();
    puts("lte_net_display_flush_test: PASS");
    return 0;
}
