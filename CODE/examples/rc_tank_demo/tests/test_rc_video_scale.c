#include <stdint.h>
#include <stddef.h>

#include "rc_video_scale.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

static int test_nearest_scale_preserves_corners_and_centres(void)
{
    const uint16_t source[12] = {
        1U, 2U, 3U, 4U,
        5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U,
    };
    uint16_t output[48] = {0};

    TEST_ASSERT(rc_video_scale_rgb565_nearest(source, 4U, 3U,
                                               output, 8U, 6U));
    TEST_ASSERT(1U == output[0]);
    TEST_ASSERT(4U == output[7]);
    TEST_ASSERT(9U == output[5U * 8U]);
    TEST_ASSERT(12U == output[5U * 8U + 7U]);
    TEST_ASSERT(6U == output[2U * 8U + 2U]);
    TEST_ASSERT(7U == output[2U * 8U + 4U]);
    return 0;
}

static int test_invalid_geometry_is_rejected(void)
{
    uint16_t pixel = 0U;
    TEST_ASSERT(!rc_video_scale_rgb565_nearest(NULL, 1U, 1U,
                                                &pixel, 1U, 1U));
    TEST_ASSERT(!rc_video_scale_rgb565_nearest(&pixel, 0U, 1U,
                                                &pixel, 1U, 1U));
    TEST_ASSERT(!rc_video_scale_rgb565_nearest(&pixel, 1U, 1U,
                                                NULL, 1U, 1U));
    return 0;
}

int main(void)
{
    int ret = test_nearest_scale_preserves_corners_and_centres();
    if (0 != ret) return ret;
    return test_invalid_geometry_is_rejected();
}
