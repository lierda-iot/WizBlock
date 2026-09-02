#include <stdint.h>

#include "camera_hal_policy.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

static int test_configured_address_wins_over_touch_ack(void)
{
    uint8_t acked[0x78] = {0};
    acked[0x15] = 1;
    acked[0x21] = 1;
    TEST_ASSERT(camera_hal_select_sensor_addr(0x21, acked, sizeof(acked)) == 0x21);
    return 0;
}

static int test_sensor_id_must_match_sp0a39(void)
{
    TEST_ASSERT(!camera_hal_sensor_id_valid(0x00, 0x00));
    TEST_ASSERT(camera_hal_sensor_id_valid(0x0A, 0x39));
    return 0;
}

static int test_sensor_output_p0_31_must_match_demo_state(void)
{
    TEST_ASSERT(!camera_hal_sensor_output_p0_31_valid(0x00));
    TEST_ASSERT(camera_hal_sensor_output_p0_31_valid(CAMERA_HAL_SP0A39_EXPECTED_P0_31));
    return 0;
}

int main(void)
{
    int result = test_configured_address_wins_over_touch_ack();
    if (result != 0) {
        return result;
    }
    result = test_sensor_id_must_match_sp0a39();
    if (result != 0) {
        return result;
    }
    return test_sensor_output_p0_31_must_match_demo_state();
}
