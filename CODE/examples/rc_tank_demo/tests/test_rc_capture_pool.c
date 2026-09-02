#include <stdbool.h>
#include <stdint.h>

#include "rc_capture_pool.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

static int test_driver_callback_order_preserves_buffer_identity(void)
{
    rc_capture_pool_t pool;
    rc_capture_pool_init(&pool);

    const int first_write = rc_capture_pool_select_write(&pool);
    TEST_ASSERT(0 == first_write);

    /* ESP-IDF requests the next DMA target before reporting the old target done. */
    const int second_write = rc_capture_pool_select_write(&pool);
    TEST_ASSERT(1 == second_write);
    TEST_ASSERT(rc_capture_pool_finish_write(&pool, first_write));

    const int first_read = rc_capture_pool_acquire_latest(&pool);
    TEST_ASSERT(first_write == first_read);
    TEST_ASSERT(RC_CAPTURE_READING == pool.slots[first_read]);
    TEST_ASSERT(RC_CAPTURE_WRITING == pool.slots[second_write]);

    return 0;
}

static int test_slow_consumer_drops_ready_never_reading(void)
{
    rc_capture_pool_t pool;
    rc_capture_pool_init(&pool);

    int writing = rc_capture_pool_select_write(&pool);
    int next_writing = rc_capture_pool_select_write(&pool);
    TEST_ASSERT(rc_capture_pool_finish_write(&pool, writing));
    int reading = rc_capture_pool_acquire_latest(&pool);
    TEST_ASSERT(reading == writing);

    writing = next_writing;
    next_writing = rc_capture_pool_select_write(&pool);
    TEST_ASSERT(rc_capture_pool_finish_write(&pool, writing));

    for (int frame = 0; frame < 20; ++frame) {
        writing = next_writing;
        next_writing = rc_capture_pool_select_write(&pool);
        TEST_ASSERT(next_writing >= 0);
        TEST_ASSERT(next_writing != reading);
        TEST_ASSERT(RC_CAPTURE_READING == pool.slots[reading]);
        TEST_ASSERT(rc_capture_pool_finish_write(&pool, writing));
    }

    TEST_ASSERT(rc_capture_pool_release_read(&pool, reading));
    TEST_ASSERT(RC_CAPTURE_FREE == pool.slots[reading]);
    return 0;
}

static int test_latest_ready_replaces_stale_ready(void)
{
    rc_capture_pool_t pool;
    rc_capture_pool_init(&pool);

    int writing = rc_capture_pool_select_write(&pool);
    int next_writing = rc_capture_pool_select_write(&pool);
    TEST_ASSERT(rc_capture_pool_finish_write(&pool, writing));
    const int stale_ready = writing;

    writing = next_writing;
    next_writing = rc_capture_pool_select_write(&pool);
    TEST_ASSERT(next_writing >= 0);
    TEST_ASSERT(rc_capture_pool_finish_write(&pool, writing));
    TEST_ASSERT(RC_CAPTURE_FREE == pool.slots[stale_ready]);
    TEST_ASSERT(writing == rc_capture_pool_acquire_latest(&pool));
    return 0;
}

static int test_incomplete_write_is_aborted_and_reusable(void)
{
    rc_capture_pool_t pool;
    rc_capture_pool_init(&pool);

    const int incomplete = rc_capture_pool_select_write(&pool);
    const int next_write = rc_capture_pool_select_write(&pool);
    TEST_ASSERT(0 == incomplete);
    TEST_ASSERT(1 == next_write);

    TEST_ASSERT(rc_capture_pool_abort_write(&pool, incomplete));
    TEST_ASSERT(RC_CAPTURE_FREE == pool.slots[incomplete]);
    TEST_ASSERT(RC_CAPTURE_WRITING == pool.slots[next_write]);
    TEST_ASSERT(-1 == rc_capture_pool_acquire_latest(&pool));
    TEST_ASSERT(incomplete == rc_capture_pool_select_write(&pool));

    TEST_ASSERT(!rc_capture_pool_abort_write(&pool, next_write + 1));
    return 0;
}

int main(void)
{
    int ret = test_driver_callback_order_preserves_buffer_identity();
    if (0 != ret) return ret;
    ret = test_slow_consumer_drops_ready_never_reading();
    if (0 != ret) return ret;
    ret = test_latest_ready_replaces_stale_ready();
    if (0 != ret) return ret;
    return test_incomplete_write_is_aborted_and_reusable();
}
