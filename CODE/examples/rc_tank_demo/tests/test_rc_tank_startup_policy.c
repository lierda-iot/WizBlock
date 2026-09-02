#include <stdbool.h>

#include "rc_tank_startup_policy.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

static int test_normal_tank_starts_all_subsystems(void)
{
    const rc_tank_startup_policy_t policy = rc_tank_startup_policy_create(false);

    TEST_ASSERT(policy.motor_enabled);
    TEST_ASSERT(policy.audio_enabled);
    TEST_ASSERT(policy.network_enabled);
    TEST_ASSERT(policy.control_enabled);
    TEST_ASSERT(policy.synthetic_camera_fallback_enabled);
    TEST_ASSERT(!policy.stop_after_preview);
    TEST_ASSERT(!policy.require_real_camera_preview);
    return 0;
}

static int test_camera_diag_a0_only_runs_real_camera_preview(void)
{
    const rc_tank_startup_policy_t policy = rc_tank_startup_policy_create(true);

    TEST_ASSERT(!policy.motor_enabled);
    TEST_ASSERT(!policy.audio_enabled);
    TEST_ASSERT(!policy.network_enabled);
    TEST_ASSERT(!policy.control_enabled);
    TEST_ASSERT(!policy.synthetic_camera_fallback_enabled);
    TEST_ASSERT(policy.stop_after_preview);
    TEST_ASSERT(policy.require_real_camera_preview);
    return 0;
}

int main(void)
{
    int ret = test_normal_tank_starts_all_subsystems();
    if (0 != ret) {
        return ret;
    }
    return test_camera_diag_a0_only_runs_real_camera_preview();
}
