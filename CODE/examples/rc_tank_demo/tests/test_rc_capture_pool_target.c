#include "rc_capture_pool_target.h"

#define TEST_ASSERT(condition) do { if (!(condition)) return __LINE__; } while (0)

static int test_prefers_pool_slot(void)
{
    rc_capture_pool_t pool = {0};
    int slot = -99;
    rc_capture_pool_init(&pool);

    TEST_ASSERT(RC_CAPTURE_TARGET_SLOT ==
                rc_capture_pool_select_target(&pool, true, &slot));
    TEST_ASSERT(slot == 0);
    TEST_ASSERT(pool.slots[0] == RC_CAPTURE_WRITING);
    return 0;
}

static int test_uses_backup_when_all_slots_are_busy(void)
{
    rc_capture_pool_t pool = {0};
    int slot = -99;
    rc_capture_pool_init(&pool);
    for (int i = 0; i < RC_CAPTURE_POOL_COUNT; ++i) {
        pool.slots[i] = RC_CAPTURE_WRITING;
    }

    TEST_ASSERT(RC_CAPTURE_TARGET_BACKUP ==
                rc_capture_pool_select_target(&pool, true, &slot));
    TEST_ASSERT(slot == -1);
    for (int i = 0; i < RC_CAPTURE_POOL_COUNT; ++i) {
        TEST_ASSERT(pool.slots[i] == RC_CAPTURE_WRITING);
    }
    return 0;
}

static int test_reports_no_target_without_backup(void)
{
    rc_capture_pool_t pool = {0};
    int slot = -99;
    rc_capture_pool_init(&pool);
    for (int i = 0; i < RC_CAPTURE_POOL_COUNT; ++i) {
        pool.slots[i] = RC_CAPTURE_WRITING;
    }

    TEST_ASSERT(RC_CAPTURE_TARGET_NONE ==
                rc_capture_pool_select_target(&pool, false, &slot));
    TEST_ASSERT(slot == -1);
    return 0;
}

int main(void)
{
    int result = test_prefers_pool_slot();
    if (0 != result) return result;
    result = test_uses_backup_when_all_slots_are_busy();
    if (0 != result) return result;
    return test_reports_no_target_without_backup();
}
