#include <stdint.h>
#include <stddef.h>

#include "rc_video_policy.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

int main(void)
{
    TEST_ASSERT(!rc_video_controller_ready(NULL));
    TEST_ASSERT(rc_video_controller_ready((const void *)(uintptr_t)1));
    return 0;
}
