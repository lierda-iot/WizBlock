#include "rc_drive_control.h"

#define TEST_ASSERT(condition) do { if (!(condition)) return __LINE__; } while (0)

static int test_full_forward_starts_at_full_output(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    const rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 0,
        .magnitude_pct = 100U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(100 == output.left_logic);
    TEST_ASSERT(100 == output.right_logic);
    TEST_ASSERT(100 == output.left_pwm_pct);
    TEST_ASSERT(100 == output.right_pwm_pct);
    return 0;
}

typedef struct {
    int16_t angle_deg;
    int16_t expected_left;
    int16_t expected_right;
} mix_case_t;

static int assert_first_logic_output(const mix_case_t *test_case)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    const rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = test_case->angle_deg,
        .magnitude_pct = 100U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(test_case->expected_left == output.left_logic);
    TEST_ASSERT(test_case->expected_right == output.right_logic);
    return 0;
}

static int test_full_magnitude_curve_interpolation_and_symmetry(void)
{
    const mix_case_t cases[] = {
        {0, 100, 100},
        {15, 100, 65},
        {30, 100, 30},
        {45, 100, -10},
        {55, 100, -57},
        {60, 100, -80},
        {70, 100, -90},
        {75, 100, -95},
        {80, 100, -97},
        {85, 100, -98},
        {90, 100, -100},
        {-30, 30, 100},
        {-90, -100, 100},
        {120, 80, -100},
        {-120, -100, 80},
        {180, -100, -100},
        {-180, -100, -100},
    };

    for (uint32_t index = 0U;
         index < (uint32_t)(sizeof(cases) / sizeof(cases[0]));
         ++index) {
        const int result = assert_first_logic_output(&cases[index]);
        if (0 != result) {
            return result;
        }
    }
    return 0;
}

static int assert_scaled_logic_output(int16_t angle_deg,
                                      uint8_t magnitude_pct,
                                      int16_t expected_left,
                                      int16_t expected_right)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    const rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = angle_deg,
        .magnitude_pct = magnitude_pct,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(expected_left == output.left_logic);
    TEST_ASSERT(expected_right == output.right_logic);
    return 0;
}

static int test_magnitude_scales_each_track_with_away_from_zero_rounding(void)
{
    int result = assert_scaled_logic_output(0, 1U, 1, 1);
    if (0 != result) return result;
    result = assert_scaled_logic_output(0, 50U, 50, 50);
    if (0 != result) return result;
    result = assert_scaled_logic_output(90, 50U, 50, -50);
    if (0 != result) return result;
    result = assert_scaled_logic_output(75, 50U, 50, -48);
    if (0 != result) return result;
    return assert_scaled_logic_output(-30, 10U, 3, 10);
}

static int get_first_output_with_config(const rc_drive_config_t *config,
                                        int16_t angle_deg,
                                        uint8_t magnitude_pct,
                                        rc_drive_output_t *output)
{
    rc_drive_controller_t controller = {0};
    const rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = angle_deg,
        .magnitude_pct = magnitude_pct,
    };

    TEST_ASSERT(rc_drive_controller_init(&controller, config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, output));
    return 0;
}

static int test_pwm_curve_and_four_direction_offsets(void)
{
    rc_drive_config_t config = {0};
    rc_drive_output_t output = {0};
    int result = 0;

    rc_drive_config_set_defaults(&config);
    for (uint32_t index = 0U; index < RC_DRIVE_SLOT_COUNT; ++index) {
        config.slots[index].start_duty_pct = 1U;
        config.slots[index].start_boost_ms = 20U;
    }

    result = get_first_output_with_config(&config, 0, 1U, &output);
    if (0 != result) return result;
    TEST_ASSERT(1 == output.left_logic && 50 == output.left_pwm_pct);
    result = get_first_output_with_config(&config, 0, 50U, &output);
    if (0 != result) return result;
    TEST_ASSERT(50 == output.left_logic && 75 == output.left_pwm_pct);
    result = get_first_output_with_config(&config, 0, 99U, &output);
    if (0 != result) return result;
    TEST_ASSERT(99 == output.left_logic && 99 == output.left_pwm_pct);
    result = get_first_output_with_config(&config, 0, 100U, &output);
    if (0 != result) return result;
    TEST_ASSERT(100 == output.left_logic && 100 == output.left_pwm_pct);

    config.slots[RC_DRIVE_SLOT_LEFT_FORWARD].duty_offset_pct = 5;
    config.slots[RC_DRIVE_SLOT_RIGHT_FORWARD].duty_offset_pct = -5;
    result = get_first_output_with_config(&config, 0, 50U, &output);
    if (0 != result) return result;
    TEST_ASSERT(80 == output.left_pwm_pct && 70 == output.right_pwm_pct);

    config.slots[RC_DRIVE_SLOT_LEFT_REVERSE].duty_offset_pct = 3;
    config.slots[RC_DRIVE_SLOT_RIGHT_REVERSE].duty_offset_pct = -4;
    result = get_first_output_with_config(&config, 180, 50U, &output);
    if (0 != result) return result;
    TEST_ASSERT(-78 == output.left_pwm_pct && -71 == output.right_pwm_pct);
    return 0;
}

static int test_init_validates_every_direction_slot(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};

    rc_drive_config_set_defaults(&config);
    for (uint32_t index = 0U; index < RC_DRIVE_SLOT_COUNT; ++index) {
        config.slots[index].duty_offset_pct = -20;
        config.slots[index].start_duty_pct = 1U;
        config.slots[index].start_boost_ms = 20U;
    }
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    for (uint32_t index = 0U; index < RC_DRIVE_SLOT_COUNT; ++index) {
        config.slots[index].duty_offset_pct = 20;
        config.slots[index].start_duty_pct = 100U;
        config.slots[index].start_boost_ms = 200U;
    }
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));

    for (uint32_t index = 0U; index < RC_DRIVE_SLOT_COUNT; ++index) {
        rc_drive_config_set_defaults(&config);
        config.slots[index].duty_offset_pct = 21;
        TEST_ASSERT(!rc_drive_controller_init(&controller, &config));
        rc_drive_config_set_defaults(&config);
        config.slots[index].start_duty_pct = 0U;
        TEST_ASSERT(!rc_drive_controller_init(&controller, &config));
        rc_drive_config_set_defaults(&config);
        config.slots[index].start_boost_ms = 30U;
        TEST_ASSERT(!rc_drive_controller_init(&controller, &config));
    }

    rc_drive_config_set_defaults(&config);
    config.slots[0].duty_offset_pct = -21;
    TEST_ASSERT(!rc_drive_controller_init(&controller, &config));
    rc_drive_config_set_defaults(&config);
    config.slots[0].start_duty_pct = 101U;
    TEST_ASSERT(!rc_drive_controller_init(&controller, &config));
    rc_drive_config_set_defaults(&config);
    config.slots[0].start_boost_ms = 220U;
    TEST_ASSERT(!rc_drive_controller_init(&controller, &config));
    return 0;
}

static int test_start_boost_only_applies_below_configured_start_duty(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 0,
        .magnitude_pct = 1U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(1 == output.left_logic && 60 == output.left_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(60 == output.left_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(60 == output.left_pwm_pct);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(1 == output.left_logic && 50 == output.left_pwm_pct);

    command.magnitude_pct = 50U;
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(50 == output.left_logic && 75 == output.left_pwm_pct);
    return 0;
}

static int test_same_direction_updates_use_fast_acceleration_and_deceleration(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 0,
        .magnitude_pct = 50U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(50 == output.left_logic);

    command.magnitude_pct = 100U;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(70 == output.left_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(90 == output.left_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(100 == output.left_logic);

    command.magnitude_pct = 20U;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(75 == output.left_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(50 == output.left_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(25 == output.left_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(20 == output.left_logic);
    return 0;
}

static int test_reversal_stops_for_40ms_then_ramps_to_opposite_target(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 90,
        .magnitude_pct = 100U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(100 == output.left_logic && -100 == output.right_logic);

    command.angle_deg = -90;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(0 == output.left_logic && 0 == output.left_pwm_pct);
    TEST_ASSERT(0 == output.right_logic && 0 == output.right_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(0 == output.left_logic && 0 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-20 == output.left_logic && 20 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-40 == output.left_logic && 40 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-60 == output.left_logic && 60 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-80 == output.left_logic && 80 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-100 == output.left_logic && 100 == output.right_logic);
    return 0;
}

static int test_tracks_ramp_and_reverse_independently(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 30,
        .magnitude_pct = 100U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(100 == output.left_logic && 30 == output.right_logic);

    command.angle_deg = 60;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(100 == output.left_logic);
    TEST_ASSERT(0 == output.right_logic && 0 == output.right_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(100 == output.left_logic && 0 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(100 == output.left_logic && -20 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-40 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-60 == output.right_logic);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-80 == output.right_logic);
    return 0;
}

static int test_stop_cancels_running_boost_and_reversal_immediately(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 0,
        .magnitude_pct = 100U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    rc_drive_controller_stop(&controller, &output);
    TEST_ASSERT(0 == output.left_logic && 0 == output.right_logic);
    TEST_ASSERT(0 == output.left_pwm_pct && 0 == output.right_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));

    command.magnitude_pct = 1U;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(60 == output.left_pwm_pct);
    rc_drive_controller_stop(&controller, &output);
    TEST_ASSERT(0 == output.left_pwm_pct && 0 == output.right_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));

    command.angle_deg = 90;
    command.magnitude_pct = 100U;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    command.angle_deg = -90;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    rc_drive_controller_stop(&controller, &output);
    TEST_ASSERT(0 == output.left_pwm_pct && 0 == output.right_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    return 0;
}

static int test_latest_target_replaces_boost_and_reversal_targets(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 0,
        .magnitude_pct = 1U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(60 == output.left_pwm_pct);
    command.magnitude_pct = 50U;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(50 == output.left_logic && 75 == output.left_pwm_pct);

    command.angle_deg = 90;
    command.magnitude_pct = 100U;
    rc_drive_controller_stop(&controller, &output);
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    command.angle_deg = -90;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-20 == output.left_logic);

    command.angle_deg = 90;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(0 == output.left_logic && 0 == output.left_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(20 == output.left_logic);
    return 0;
}

static int test_direction_change_during_boost_observes_deadtime_and_reverse_boost(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 0,
        .magnitude_pct = 1U,
    };

    rc_drive_config_set_defaults(&config);
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(1 == output.left_logic && 60 == output.left_pwm_pct);

    command.angle_deg = 180;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(0 == output.left_logic && 0 == output.left_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(0 == output.left_logic && 0 == output.left_pwm_pct);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-1 == output.left_logic && -60 == output.left_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-60 == output.left_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-60 == output.left_pwm_pct);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-1 == output.left_logic && -50 == output.left_pwm_pct);
    return 0;
}

static int test_small_reverse_track_target_during_boost_observes_deadtime(void)
{
    rc_drive_config_t config = {0};
    rc_drive_controller_t controller = {0};
    rc_drive_output_t output = {0};
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 0,
        .magnitude_pct = 50U,
    };

    rc_drive_config_set_defaults(&config);
    config.slots[RC_DRIVE_SLOT_LEFT_FORWARD].duty_offset_pct = -20;
    config.slots[RC_DRIVE_SLOT_RIGHT_FORWARD].duty_offset_pct = -20;
    TEST_ASSERT(rc_drive_controller_init(&controller, &config));
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(50 == output.right_logic && 60 == output.right_pwm_pct);

    command.angle_deg = 45;
    command.magnitude_pct = 100U;
    rc_drive_controller_set_target(&controller, &command);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(0 == output.right_logic && 0 == output.right_pwm_pct);
    TEST_ASSERT(!rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(0 == output.right_logic && 0 == output.right_pwm_pct);
    TEST_ASSERT(rc_drive_controller_step(&controller, &output));
    TEST_ASSERT(-10 == output.right_logic && -60 == output.right_pwm_pct);
    return 0;
}

int main(void)
{
    int result = test_full_forward_starts_at_full_output();
    if (0 != result) {
        return result;
    }
    result = test_full_magnitude_curve_interpolation_and_symmetry();
    if (0 != result) {
        return result;
    }
    result = test_magnitude_scales_each_track_with_away_from_zero_rounding();
    if (0 != result) {
        return result;
    }
    result = test_pwm_curve_and_four_direction_offsets();
    if (0 != result) {
        return result;
    }
    result = test_init_validates_every_direction_slot();
    if (0 != result) {
        return result;
    }
    result = test_start_boost_only_applies_below_configured_start_duty();
    if (0 != result) {
        return result;
    }
    result = test_same_direction_updates_use_fast_acceleration_and_deceleration();
    if (0 != result) {
        return result;
    }
    result = test_reversal_stops_for_40ms_then_ramps_to_opposite_target();
    if (0 != result) {
        return result;
    }
    result = test_tracks_ramp_and_reverse_independently();
    if (0 != result) {
        return result;
    }
    result = test_stop_cancels_running_boost_and_reversal_immediately();
    if (0 != result) {
        return result;
    }
    result = test_latest_target_replaces_boost_and_reversal_targets();
    if (0 != result) {
        return result;
    }
    result = test_direction_change_during_boost_observes_deadtime_and_reverse_boost();
    if (0 != result) {
        return result;
    }
    return test_small_reverse_track_target_during_boost_observes_deadtime();
}
