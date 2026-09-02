#include "rc_video_latest_frame.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

static int test_prefers_free_slot(void)
{
    TEST_ASSERT(rc_video_latest_frame_select_slot(0x2U, 0x1U) == 1);
    TEST_ASSERT(rc_video_latest_frame_select_slot(0x1U, 0x2U) == 0);
    return 0;
}

static int test_drops_oldest_ready_when_no_free_slot(void)
{
    TEST_ASSERT(rc_video_latest_frame_select_slot(0U, 0x1U) == 0);
    TEST_ASSERT(rc_video_latest_frame_select_slot(0U, 0x2U) == 1);
    return 0;
}

static int test_preserves_display_slot(void)
{
    TEST_ASSERT(rc_video_latest_frame_select_slot(0U, 0U) == -1);
    return 0;
}

int main(void)
{
    int ret = test_prefers_free_slot();
    if (ret != 0) return ret;
    ret = test_drops_oldest_ready_when_no_free_slot();
    if (ret != 0) return ret;
    return test_preserves_display_slot();
}
