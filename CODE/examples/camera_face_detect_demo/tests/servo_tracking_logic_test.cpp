#include <cassert>

#include "servo_tracking_logic.h"

static constexpr int TEST_MIN_CENTER_Y = 0;
static constexpr int TEST_MAX_CENTER_Y = 319;
static constexpr int TEST_SCREEN_CENTER_Y = 160;
static constexpr int TEST_MIN_ANGLE_DEG = 45;
static constexpr int TEST_CENTER_ANGLE_DEG = 90;
static constexpr int TEST_MAX_ANGLE_DEG = 135;
static constexpr int TEST_DEADZONE_PX = 18;
static constexpr int TEST_GAIN_NUM = 1;
static constexpr int TEST_GAIN_DEN = 4;
static constexpr int TEST_DIRECTION_SIGN = 1;
static constexpr int TEST_TARGET_STEP_MAX_DEG = 8;
static constexpr int TEST_APPLY_STEP_MAX_DEG = 3;
static constexpr uint32_t TEST_STALE_AFTER_MS = 1500;
static constexpr uint32_t TEST_RETURN_CENTER_AFTER_MS = 4000;
static constexpr uint64_t US_PER_MS = 1000;

static servo_control_config_t make_config(void)
{
    servo_control_config_t config = {};
    config.min_center_y = TEST_MIN_CENTER_Y;
    config.max_center_y = TEST_MAX_CENTER_Y;
    config.screen_center_y = TEST_SCREEN_CENTER_Y;
    config.min_angle_deg = TEST_MIN_ANGLE_DEG;
    config.center_angle_deg = TEST_CENTER_ANGLE_DEG;
    config.max_angle_deg = TEST_MAX_ANGLE_DEG;
    config.deadzone_px = TEST_DEADZONE_PX;
    config.gain_num = TEST_GAIN_NUM;
    config.gain_den = TEST_GAIN_DEN;
    config.direction_sign = TEST_DIRECTION_SIGN;
    config.target_step_max_deg = TEST_TARGET_STEP_MAX_DEG;
    config.apply_step_max_deg = TEST_APPLY_STEP_MAX_DEG;
    config.stale_after_ms = TEST_STALE_AFTER_MS;
    config.return_center_after_ms = TEST_RETURN_CENTER_AFTER_MS;
    return config;
}

static servo_face_input_t make_input(uint32_t sequence,
                                     uint64_t completed_at_ms,
                                     bool valid,
                                     int center_y)
{
    servo_face_input_t input = {};
    input.sequence = sequence;
    input.completed_at_us = completed_at_ms * US_PER_MS;
    input.valid = valid;
    input.center_y = (int16_t)center_y;
    return input;
}

static void test_latest_result_replaces_unfinished_target(void)
{
    const servo_control_config_t config = make_config();
    servo_control_state_t state = {};
    init_servo_control_state(&state, &config);

    servo_face_input_t input = make_input(1, 0, true, 80);
    servo_control_step_t step = step_servo_control(&state, &config, &input, 0);
    assert(step.input_consumed);
    assert(step.input_usable);
    assert(!step.superseded);
    assert(80 == step.screen_error_px);
    assert(SERVO_CONTROL_TRACK == state.mode);
    assert(98 == state.target_angle_deg);
    assert(93 == state.current_angle_deg);

    input = make_input(2, 120, true, 240);
    step = step_servo_control(&state, &config, &input, 120 * US_PER_MS);
    assert(step.input_consumed);
    assert(step.superseded);
    assert(-80 == step.screen_error_px);
    assert(85 == state.target_angle_deg);
    assert(90 == state.current_angle_deg);
    assert(2U == state.applied_sequence);
}

static void test_same_sequence_does_not_recompute_from_mutated_input(void)
{
    const servo_control_config_t config = make_config();
    servo_control_state_t state = {};
    init_servo_control_state(&state, &config);

    servo_face_input_t input = make_input(10, 0, true, 80);
    servo_control_step_t step = step_servo_control(&state, &config, &input, 0);
    assert(98 == state.target_angle_deg);
    assert(93 == state.current_angle_deg);

    input.center_y = 240;
    step = step_servo_control(&state, &config, &input, 120 * US_PER_MS);
    assert(!step.input_consumed);
    assert(98 == state.target_angle_deg);
    assert(96 == state.current_angle_deg);
}

static void test_deadzone_and_invalid_result_cancel_old_target(void)
{
    const servo_control_config_t config = make_config();
    servo_control_state_t state = {};
    init_servo_control_state(&state, &config);

    servo_face_input_t input = make_input(1, 0, true, 80);
    (void)step_servo_control(&state, &config, &input, 0);
    assert(98 == state.target_angle_deg);
    assert(93 == state.current_angle_deg);

    input = make_input(2, 120, true, TEST_SCREEN_CENTER_Y - TEST_DEADZONE_PX);
    servo_control_step_t step = step_servo_control(&state, &config, &input, 120 * US_PER_MS);
    assert(step.superseded);
    assert(SERVO_CONTROL_HOLD == state.mode);
    assert(93 == state.target_angle_deg);
    assert(93 == state.current_angle_deg);

    input = make_input(3, 240, true, 80);
    (void)step_servo_control(&state, &config, &input, 240 * US_PER_MS);
    assert(101 == state.target_angle_deg);
    assert(96 == state.current_angle_deg);

    input = make_input(4, 360, false, -1);
    step = step_servo_control(&state, &config, &input, 360 * US_PER_MS);
    assert(step.superseded);
    assert(!step.input_usable);
    assert(SERVO_CONTROL_HOLD == state.mode);
    assert(96 == state.target_angle_deg);
    assert(96 == state.current_angle_deg);
}

static void test_stale_result_stops_motion_and_lost_face_returns_center(void)
{
    const servo_control_config_t config = make_config();
    servo_control_state_t state = {};
    init_servo_control_state(&state, &config);

    servo_face_input_t input = make_input(1, 0, true, 80);
    (void)step_servo_control(&state, &config, &input, 0);
    assert(93 == state.current_angle_deg);

    servo_control_step_t step = step_servo_control(&state,
                                                    &config,
                                                    &input,
                                                    TEST_STALE_AFTER_MS * US_PER_MS);
    assert(step.input_usable);
    assert(96 == state.current_angle_deg);

    step = step_servo_control(&state,
                              &config,
                              &input,
                              (TEST_STALE_AFTER_MS * US_PER_MS) + 1U);
    assert(!step.input_usable);
    assert(step.input_expired);
    assert(SERVO_CONTROL_HOLD == state.mode);
    assert(96 == state.target_angle_deg);
    assert(96 == state.current_angle_deg);

    step = step_servo_control(&state,
                              &config,
                              &input,
                              (TEST_RETURN_CENTER_AFTER_MS * US_PER_MS) - 1U);
    assert(SERVO_CONTROL_HOLD == state.mode);
    assert(96 == state.current_angle_deg);

    step = step_servo_control(&state,
                              &config,
                              &input,
                              TEST_RETURN_CENTER_AFTER_MS * US_PER_MS);
    assert(SERVO_CONTROL_RETURN_CENTER == state.mode);
    assert(TEST_CENTER_ANGLE_DEG == state.target_angle_deg);
    assert(93 == state.current_angle_deg);
}

static void test_safe_angle_and_pulse_limits(void)
{
    const servo_control_config_t config = make_config();
    servo_control_state_t state = {};
    init_servo_control_state(&state, &config);

    state.current_angle_deg = TEST_MIN_ANGLE_DEG + 2;
    state.target_angle_deg = state.current_angle_deg;
    servo_face_input_t input = make_input(1, 0, true, 240);
    (void)step_servo_control(&state, &config, &input, 0);
    assert(TEST_MIN_ANGLE_DEG == state.target_angle_deg);
    assert(TEST_MIN_ANGLE_DEG == state.current_angle_deg);

    state.current_angle_deg = TEST_MAX_ANGLE_DEG - 2;
    state.target_angle_deg = state.current_angle_deg;
    input = make_input(2, 120, true, 80);
    (void)step_servo_control(&state, &config, &input, 120 * US_PER_MS);
    assert(TEST_MAX_ANGLE_DEG == state.target_angle_deg);
    assert(TEST_MAX_ANGLE_DEG == state.current_angle_deg);

    assert(1000U == map_servo_angle_to_pulse_us(TEST_MIN_ANGLE_DEG,
                                                TEST_MIN_ANGLE_DEG,
                                                TEST_MAX_ANGLE_DEG,
                                                1000,
                                                2000));
    assert(1500U == map_servo_angle_to_pulse_us(TEST_CENTER_ANGLE_DEG,
                                                TEST_MIN_ANGLE_DEG,
                                                TEST_MAX_ANGLE_DEG,
                                                1000,
                                                2000));
    assert(2000U == map_servo_angle_to_pulse_us(TEST_MAX_ANGLE_DEG,
                                                TEST_MIN_ANGLE_DEG,
                                                TEST_MAX_ANGLE_DEG,
                                                1000,
                                                2000));
}

int main(void)
{
    test_latest_result_replaces_unfinished_target();
    test_same_sequence_does_not_recompute_from_mutated_input();
    test_deadzone_and_invalid_result_cancel_old_target();
    test_stale_result_stops_motion_and_lost_face_returns_center();
    test_safe_angle_and_pulse_limits();
    return 0;
}
