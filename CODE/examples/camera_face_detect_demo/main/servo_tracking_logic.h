#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t requested_delta_deg;
    int32_t target_angle_deg;
} servo_track_command_t;

typedef enum {
    SERVO_CONTROL_HOLD = 0,
    SERVO_CONTROL_TRACK,
    SERVO_CONTROL_RETURN_CENTER,
} servo_control_mode_t;

typedef struct {
    int32_t min_center_y;
    int32_t max_center_y;
    int32_t screen_center_y;
    int32_t min_angle_deg;
    int32_t center_angle_deg;
    int32_t max_angle_deg;
    int32_t deadzone_px;
    int32_t gain_num;
    int32_t gain_den;
    int32_t direction_sign;
    int32_t target_step_max_deg;
    int32_t apply_step_max_deg;
    uint32_t stale_after_ms;
    uint32_t return_center_after_ms;
} servo_control_config_t;

typedef struct {
    uint32_t sequence;
    uint64_t completed_at_us;
    bool valid;
    int16_t center_y;
} servo_face_input_t;

typedef struct {
    int32_t current_angle_deg;
    int32_t target_angle_deg;
    int32_t latest_center_y;
    uint32_t applied_sequence;
    uint64_t latest_completed_at_us;
    uint64_t last_valid_at_us;
    servo_control_mode_t mode;
    bool has_seen_face;
    bool latest_input_valid;
    bool latest_input_usable;
} servo_control_state_t;

typedef struct {
    int32_t screen_error_px;
    int32_t current_before_deg;
    int32_t target_before_deg;
    int32_t applied_delta_deg;
    bool input_consumed;
    bool input_usable;
    bool input_expired;
    bool superseded;
    bool target_changed;
    bool output_changed;
    bool mode_changed;
} servo_control_step_t;

#define SERVO_US_PER_MS 1000ULL

static inline int32_t clamp_servo_value(const int32_t value,
                                        const int32_t min_value,
                                        const int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static inline int32_t move_servo_angle_toward(const int32_t current,
                                              const int32_t target,
                                              const int32_t max_step)
{
    if (0 >= max_step) {
        return current;
    }
    if (current < target) {
        return clamp_servo_value(current + max_step, current, target);
    }
    if (current > target) {
        return clamp_servo_value(current - max_step, target, current);
    }
    return current;
}

static inline servo_track_command_t make_servo_track_command(const int32_t current_angle,
                                                              const int32_t face_dx,
                                                              const int32_t deadzone_px,
                                                              const int32_t gain_num,
                                                              const int32_t gain_den,
                                                              const int32_t direction_sign,
                                                              const int32_t max_step_deg,
                                                              const int32_t min_angle_deg,
                                                              const int32_t max_angle_deg)
{
    const int32_t bounded_current = clamp_servo_value(current_angle, min_angle_deg, max_angle_deg);
    servo_track_command_t command = {
        .requested_delta_deg = 0,
        .target_angle_deg = bounded_current,
    };

    const int32_t abs_face_dx = (0 > face_dx) ? -face_dx : face_dx;
    if ((abs_face_dx <= deadzone_px) || (0 >= gain_den) || (0 >= max_step_deg) || (0 == direction_sign)) {
        return command;
    }

    const int32_t signed_dx = face_dx * direction_sign;
    int32_t requested_delta = (signed_dx * gain_num) / gain_den;
    if (0 == requested_delta) {
        requested_delta = (0 < signed_dx) ? 1 : -1;
    }
    requested_delta = clamp_servo_value(requested_delta, -max_step_deg, max_step_deg);

    command.target_angle_deg = clamp_servo_value(bounded_current + requested_delta,
                                                  min_angle_deg,
                                                  max_angle_deg);
    command.requested_delta_deg = command.target_angle_deg - bounded_current;
    return command;
}

static inline uint32_t map_servo_angle_to_pulse_us(const int32_t angle_deg,
                                                    const int32_t min_angle_deg,
                                                    const int32_t max_angle_deg,
                                                    const uint32_t min_pulse_us,
                                                    const uint32_t max_pulse_us)
{
    if ((max_angle_deg <= min_angle_deg) || (max_pulse_us <= min_pulse_us)) {
        return min_pulse_us;
    }

    const int32_t bounded_angle = clamp_servo_value(angle_deg, min_angle_deg, max_angle_deg);
    const uint32_t angle_span = (uint32_t)(max_angle_deg - min_angle_deg);
    const uint32_t pulse_span = max_pulse_us - min_pulse_us;
    const uint64_t scaled = (uint64_t)(bounded_angle - min_angle_deg) * pulse_span;
    return min_pulse_us + (uint32_t)((scaled + (angle_span / 2U)) / angle_span);
}

static inline bool is_servo_control_config_valid(const servo_control_config_t *const config)
{
    if (NULL == config) {
        return false;
    }
    return (config->min_center_y < config->max_center_y) &&
           (config->min_center_y <= config->screen_center_y) &&
           (config->screen_center_y <= config->max_center_y) &&
           (config->min_angle_deg < config->center_angle_deg) &&
           (config->center_angle_deg < config->max_angle_deg) &&
           (0 <= config->deadzone_px) &&
           (0 < config->gain_num) &&
           (0 < config->gain_den) &&
           (0 != config->direction_sign) &&
           (0 < config->target_step_max_deg) &&
           (0 < config->apply_step_max_deg) &&
           (0U < config->stale_after_ms) &&
           (config->stale_after_ms < config->return_center_after_ms);
}

static inline uint64_t get_servo_input_age_ms(const uint64_t now_us,
                                               const uint64_t completed_at_us)
{
    if (now_us < completed_at_us) {
        return UINT64_MAX;
    }
    return (now_us - completed_at_us) / SERVO_US_PER_MS;
}

static inline bool is_servo_face_input_usable(const servo_face_input_t *const input,
                                              const servo_control_config_t *const config,
                                              const uint64_t now_us)
{
    if ((NULL == input) || !is_servo_control_config_valid(config)) {
        return false;
    }
    if ((0U == input->sequence) || !input->valid ||
        (input->center_y < config->min_center_y) ||
        (input->center_y > config->max_center_y)) {
        return false;
    }
    if (now_us < input->completed_at_us) {
        return false;
    }
    return (now_us - input->completed_at_us) <=
           ((uint64_t)config->stale_after_ms * SERVO_US_PER_MS);
}

static inline void init_servo_control_state(servo_control_state_t *const state,
                                            const servo_control_config_t *const config)
{
    if (NULL == state) {
        return;
    }

    servo_control_state_t initial_state = {};
    initial_state.mode = SERVO_CONTROL_HOLD;
    initial_state.latest_center_y = -1;
    if (is_servo_control_config_valid(config)) {
        initial_state.current_angle_deg = config->center_angle_deg;
        initial_state.target_angle_deg = config->center_angle_deg;
    }
    *state = initial_state;
}

static inline bool is_latest_servo_input_usable(const servo_control_state_t *const state,
                                                const servo_control_config_t *const config,
                                                const uint64_t now_us)
{
    if ((NULL == state) || !is_servo_control_config_valid(config) ||
        !state->latest_input_valid || (0U == state->applied_sequence)) {
        return false;
    }
    if (now_us < state->latest_completed_at_us) {
        return false;
    }
    return (now_us - state->latest_completed_at_us) <=
           ((uint64_t)config->stale_after_ms * SERVO_US_PER_MS);
}

static inline servo_control_step_t step_servo_control(servo_control_state_t *const state,
                                                      const servo_control_config_t *const config,
                                                      const servo_face_input_t *const latest_input,
                                                      const uint64_t now_us)
{
    servo_control_step_t step = {};
    if ((NULL == state) || (NULL == latest_input) || !is_servo_control_config_valid(config)) {
        return step;
    }

    step.current_before_deg = state->current_angle_deg;
    step.target_before_deg = state->target_angle_deg;
    const servo_control_mode_t mode_before = state->mode;
    const bool has_new_input = (0U != latest_input->sequence) &&
                               (latest_input->sequence != state->applied_sequence);

    if (has_new_input) {
        step.input_consumed = true;
        step.superseded = (state->current_angle_deg != state->target_angle_deg);
        state->applied_sequence = latest_input->sequence;
        state->latest_completed_at_us = latest_input->completed_at_us;
        state->latest_center_y = latest_input->center_y;
        state->latest_input_valid = latest_input->valid &&
                                    (latest_input->center_y >= config->min_center_y) &&
                                    (latest_input->center_y <= config->max_center_y);
        state->latest_input_usable = is_servo_face_input_usable(latest_input, config, now_us);

        if (state->latest_input_usable) {
            state->has_seen_face = true;
            state->last_valid_at_us = latest_input->completed_at_us;
            step.screen_error_px = config->screen_center_y - latest_input->center_y;
            const servo_track_command_t command = make_servo_track_command(
                state->current_angle_deg,
                step.screen_error_px,
                config->deadzone_px,
                config->gain_num,
                config->gain_den,
                config->direction_sign,
                config->target_step_max_deg,
                config->min_angle_deg,
                config->max_angle_deg);
            state->target_angle_deg = command.target_angle_deg;
            state->mode = (0 == command.requested_delta_deg) ? SERVO_CONTROL_HOLD : SERVO_CONTROL_TRACK;
        } else {
            state->target_angle_deg = state->current_angle_deg;
            state->mode = SERVO_CONTROL_HOLD;
        }
    } else {
        const bool was_usable = state->latest_input_usable;
        state->latest_input_usable = is_latest_servo_input_usable(state, config, now_us);
        if (was_usable && !state->latest_input_usable) {
            step.input_expired = true;
            state->target_angle_deg = state->current_angle_deg;
            state->mode = SERVO_CONTROL_HOLD;
        }
    }

    step.input_usable = state->latest_input_usable;
    if (!state->latest_input_usable && state->has_seen_face &&
        (now_us >= state->last_valid_at_us)) {
        const uint64_t lost_ms = (now_us - state->last_valid_at_us) / SERVO_US_PER_MS;
        if (lost_ms >= config->return_center_after_ms) {
            state->target_angle_deg = config->center_angle_deg;
            state->mode = SERVO_CONTROL_RETURN_CENTER;
        }
    }

    step.target_changed = (step.target_before_deg != state->target_angle_deg);
    step.mode_changed = (mode_before != state->mode);
    state->current_angle_deg = move_servo_angle_toward(state->current_angle_deg,
                                                       state->target_angle_deg,
                                                       config->apply_step_max_deg);
    step.applied_delta_deg = state->current_angle_deg - step.current_before_deg;
    step.output_changed = (0 != step.applied_delta_deg);
    return step;
}
