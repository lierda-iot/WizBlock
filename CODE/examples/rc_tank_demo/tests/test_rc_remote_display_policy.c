#include <stdbool.h>

#include "rc_remote_display_policy.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

int main(void)
{
    rc_remote_display_policy_t policy = {0};
    TEST_ASSERT(rc_remote_display_should_present(&policy, false, 0, 0, false, 0U));
    TEST_ASSERT(!rc_remote_display_should_present(&policy, false, 0, 0, false, 0U));
    TEST_ASSERT(rc_remote_display_should_present(&policy, false, 0, 0, false, 1U));
    TEST_ASSERT(rc_remote_display_should_present(&policy, false, 12, -8, true, 1U));
    TEST_ASSERT(rc_remote_display_should_present(&policy, true, 12, -8, true, 1U));
    return 0;
}
