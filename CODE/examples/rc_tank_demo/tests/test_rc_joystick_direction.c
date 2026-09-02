#include "rc_joystick.h"

#define TEST_ASSERT(condition) do { if (!(condition)) return __LINE__; } while (0)

static int test_axis_radius_boundaries_map_to_stop_and_full_drive_range(void)
{
    rc_ctrl_command_t command = {0};

    rc_joystick_command_from_offset(0, -9, &command);
    TEST_ASSERT(RC_CTRL_MODE_STOP == command.mode);
    TEST_ASSERT(0 == command.angle_deg && 0U == command.magnitude_pct);

    rc_joystick_command_from_offset(0, -10, &command);
    TEST_ASSERT(RC_CTRL_MODE_DRIVE == command.mode);
    TEST_ASSERT(0 == command.angle_deg && 1U == command.magnitude_pct);

    rc_joystick_command_from_offset(0, -62, &command);
    TEST_ASSERT(RC_CTRL_MODE_DRIVE == command.mode);
    TEST_ASSERT(0 == command.angle_deg && 100U == command.magnitude_pct);

    rc_joystick_command_from_offset(0, -63, &command);
    TEST_ASSERT(RC_CTRL_MODE_DRIVE == command.mode);
    TEST_ASSERT(0 == command.angle_deg && 100U == command.magnitude_pct);
    return 0;
}

typedef struct {
    int dx;
    int dy;
    int16_t expected_angle_deg;
} angle_case_t;

static int test_angle_uses_nearest_degree_in_all_quadrants(void)
{
    const angle_case_t cases[] = {
        {0, -20, 0},
        {20, 0, 90},
        {-20, 0, -90},
        {0, 20, 180},
        {20, -20, 45},
        {-20, -20, -45},
        {20, 20, 135},
        {-20, 20, -135},
        {10, -20, 27},
        {-10, -20, -27},
        {10, 20, 153},
        {-10, 20, -153},
        {8, -20, 22},
        {9, -21, 23},
    };
    rc_ctrl_command_t command = {0};

    for (uint32_t index = 0U;
         index < (uint32_t)(sizeof(cases) / sizeof(cases[0]));
         ++index) {
        rc_joystick_command_from_offset(cases[index].dx,
                                        cases[index].dy,
                                        &command);
        TEST_ASSERT(RC_CTRL_MODE_DRIVE == command.mode);
        TEST_ASSERT(cases[index].expected_angle_deg == command.angle_deg);
    }
    return 0;
}

static int test_offset_clamp_uses_a_true_circle(void)
{
    int dx = 10;
    int dy = 10;

    rc_joystick_clamp_offset(&dx, &dy, RC_JOY_MAX_TRAVEL);
    TEST_ASSERT(10 == dx && 10 == dy);

    dx = 100;
    dy = 0;
    rc_joystick_clamp_offset(&dx, &dy, RC_JOY_MAX_TRAVEL);
    TEST_ASSERT(RC_JOY_MAX_TRAVEL == dx && 0 == dy);

    dx = 80;
    dy = 80;
    rc_joystick_clamp_offset(&dx, &dy, RC_JOY_MAX_TRAVEL);
    TEST_ASSERT((dx * dx + dy * dy) <=
                (RC_JOY_MAX_TRAVEL * RC_JOY_MAX_TRAVEL));
    TEST_ASSERT(1 >= ((dx > dy) ? (dx - dy) : (dy - dx)));

    dx = -100;
    dy = -50;
    rc_joystick_clamp_offset(&dx, &dy, RC_JOY_MAX_TRAVEL);
    TEST_ASSERT(-55 == dx && -27 == dy);
    TEST_ASSERT((dx * dx + dy * dy) <=
                (RC_JOY_MAX_TRAVEL * RC_JOY_MAX_TRAVEL));
    return 0;
}

static int test_touch_mapping_capture_drag_and_release_return_continuous_command(void)
{
    rc_joystick_input_t input = {0};
    int screen_x = -1;
    int screen_y = -1;

    rc_joystick_map_touch_to_screen(136, 211, &screen_x, &screen_y);
    TEST_ASSERT(RC_JOY_BASE_CX == screen_x && RC_JOY_BASE_CY == screen_y);

    rc_joystick_resolve_touch(false, 0, 0, &input);
    TEST_ASSERT(!input.active);
    TEST_ASSERT(RC_CTRL_MODE_STOP == input.command.mode);

    rc_joystick_resolve_touch(true, 0, 0, &input);
    TEST_ASSERT(!input.active);
    TEST_ASSERT(RC_CTRL_MODE_STOP == input.command.mode);

    rc_joystick_resolve_touch(true, 96, 211, &input);
    TEST_ASSERT(input.active && RC_CTRL_MODE_DRIVE == input.command.mode);
    TEST_ASSERT(0 == input.command.angle_deg);
    rc_joystick_resolve_touch(true, 176, 211, &input);
    TEST_ASSERT(input.active && 180 == input.command.angle_deg);
    rc_joystick_resolve_touch(true, 136, 251, &input);
    TEST_ASSERT(input.active && -90 == input.command.angle_deg);
    rc_joystick_resolve_touch(true, 136, 171, &input);
    TEST_ASSERT(input.active && 90 == input.command.angle_deg);

    rc_joystick_resolve_touch(true, 0, 0, &input);
    TEST_ASSERT(input.active);
    TEST_ASSERT((input.knob_dx * input.knob_dx +
                 input.knob_dy * input.knob_dy) <=
                (RC_JOY_MAX_TRAVEL * RC_JOY_MAX_TRAVEL));

    rc_joystick_resolve_touch(false, 0, 0, &input);
    TEST_ASSERT(!input.active);
    TEST_ASSERT(0 == input.knob_dx && 0 == input.knob_dy);
    TEST_ASSERT(RC_CTRL_MODE_STOP == input.command.mode);
    return 0;
}

static int test_deadzone_uses_true_radial_distance_before_rounding(void)
{
    rc_ctrl_command_t command = {0};

    rc_joystick_command_from_offset(9, 4, &command);
    TEST_ASSERT(RC_CTRL_MODE_STOP == command.mode);
    rc_joystick_command_from_offset(6, 8, &command);
    TEST_ASSERT(RC_CTRL_MODE_DRIVE == command.mode);
    TEST_ASSERT(1U == command.magnitude_pct);
    TEST_ASSERT(143 == command.angle_deg);
    return 0;
}

int main(void)
{
    int result = test_axis_radius_boundaries_map_to_stop_and_full_drive_range();
    if (0 != result) {
        return result;
    }
    result = test_angle_uses_nearest_degree_in_all_quadrants();
    if (0 != result) {
        return result;
    }
    result = test_offset_clamp_uses_a_true_circle();
    if (0 != result) {
        return result;
    }
    result = test_touch_mapping_capture_drag_and_release_return_continuous_command();
    if (0 != result) {
        return result;
    }
    return test_deadzone_uses_true_radial_distance_before_rounding();
}
