#include <stdint.h>

#include "rc_control_tx_policy.h"

#define TEST_ASSERT(condition) do { if (!(condition)) return __LINE__; } while (0)

static int test_semantic_changes_heartbeat_and_success_commit_policy(void)
{
    rc_control_tx_policy_t policy = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 0,
        .magnitude_pct = 50U,
    };

    TEST_ASSERT(rc_control_tx_should_send(&policy, &command, 0U));
    TEST_ASSERT(0U == rc_control_tx_sequence(&policy));

    /* Failed send: no mark call, so next cycle retries the same sequence. */
    TEST_ASSERT(rc_control_tx_should_send(&policy, &command, 20U));
    TEST_ASSERT(0U == rc_control_tx_sequence(&policy));

    rc_control_tx_mark_sent(&policy, &command, 20U);
    TEST_ASSERT(1U == rc_control_tx_sequence(&policy));
    TEST_ASSERT(!rc_control_tx_should_send(&policy, &command, 40U));
    TEST_ASSERT(rc_control_tx_should_send(&policy, &command, 120U));

    rc_control_tx_mark_sent(&policy, &command, 120U);
    command.angle_deg = 1;
    TEST_ASSERT(rc_control_tx_should_send(&policy, &command, 140U));
    rc_control_tx_mark_sent(&policy, &command, 140U);
    command.magnitude_pct = 51U;
    TEST_ASSERT(rc_control_tx_should_send(&policy, &command, 160U));
    rc_control_tx_mark_sent(&policy, &command, 160U);
    command.mode = RC_CTRL_MODE_STOP;
    command.angle_deg = 0;
    command.magnitude_pct = 0U;
    TEST_ASSERT(rc_control_tx_should_send(&policy, &command, 180U));

    rc_control_tx_reset(&policy);
    TEST_ASSERT(rc_control_tx_should_send(&policy, &command, 181U));
    TEST_ASSERT(0U == rc_control_tx_sequence(&policy));
    return 0;
}

static int test_time_and_sequence_wraparound_are_natural(void)
{
    rc_control_tx_policy_t policy = {0};
    rc_ctrl_command_t stop = {
        .mode = RC_CTRL_MODE_STOP,
        .angle_deg = 90,
        .magnitude_pct = 100U,
    };

    rc_control_tx_mark_sent(&policy, &stop, UINT32_MAX - 50U);
    stop.angle_deg = 0;
    stop.magnitude_pct = 0U;
    TEST_ASSERT(!rc_control_tx_should_send(&policy, &stop, 48U));
    TEST_ASSERT(rc_control_tx_should_send(&policy, &stop, 49U));

    rc_control_tx_reset(&policy);
    for (uint32_t count = 0U; count < 65536U; ++count) {
        rc_control_tx_mark_sent(&policy, &stop, count);
    }
    TEST_ASSERT(0U == rc_control_tx_sequence(&policy));
    return 0;
}

int main(void)
{
    int result = test_semantic_changes_heartbeat_and_success_commit_policy();
    if (0 != result) {
        return result;
    }
    return test_time_and_sequence_wraparound_are_natural();
}
