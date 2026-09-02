#include "rc_drive_control.h"

#include <stddef.h>

typedef struct {
    int16_t angle_deg;
    int16_t left_logic;
    int16_t right_logic;
} rc_drive_mix_node_t;

static const rc_drive_mix_node_t s_mix_nodes[] = {
    {0, 100, 100},
    {15, 100, 65},
    {30, 100, 30},
    {45, 100, -10},
    {60, 100, -80},
    {75, 100, -95},
    {90, 100, -100},
};

#define RC_DRIVE_MIX_NODE_COUNT \
    ((uint32_t)(sizeof(s_mix_nodes) / sizeof(s_mix_nodes[0])))

static int16_t divide_round_away_from_zero(int32_t numerator,
                                           int32_t denominator)
{
    if (0 <= numerator) {
        return (int16_t)((numerator + (denominator / 2)) / denominator);
    }
    return (int16_t)(-((-numerator + (denominator / 2)) / denominator));
}

static int16_t interpolate_mix_value(int16_t angle_deg,
                                     int16_t start_angle,
                                     int16_t end_angle,
                                     int16_t start_value,
                                     int16_t end_value)
{
    const int32_t span = end_angle - start_angle;
    const int32_t offset = angle_deg - start_angle;
    const int32_t numerator = ((int32_t)start_value * span) +
                              ((int32_t)(end_value - start_value) * offset);
    return divide_round_away_from_zero(numerator, span);
}

static void mix_forward_right(int16_t angle_deg,
                              int16_t *left_logic,
                              int16_t *right_logic)
{
    uint32_t node_index = 1U;

    for (node_index = 1U;
         node_index < RC_DRIVE_MIX_NODE_COUNT;
         ++node_index) {
        if (angle_deg <= s_mix_nodes[node_index].angle_deg) {
            const rc_drive_mix_node_t *start = &s_mix_nodes[node_index - 1U];
            const rc_drive_mix_node_t *end = &s_mix_nodes[node_index];
            *left_logic = interpolate_mix_value(angle_deg,
                                                start->angle_deg,
                                                end->angle_deg,
                                                start->left_logic,
                                                end->left_logic);
            *right_logic = interpolate_mix_value(angle_deg,
                                                 start->angle_deg,
                                                 end->angle_deg,
                                                 start->right_logic,
                                                 end->right_logic);
            return;
        }
    }
    *left_logic = s_mix_nodes[RC_DRIVE_MIX_NODE_COUNT - 1U].left_logic;
    *right_logic = s_mix_nodes[RC_DRIVE_MIX_NODE_COUNT - 1U].right_logic;
}

static void mix_full_magnitude(int16_t angle_deg,
                               int16_t *left_logic,
                               int16_t *right_logic)
{
    const bool is_left = (0 > angle_deg);
    const int16_t absolute_angle = is_left ? (int16_t)-angle_deg : angle_deg;
    const bool is_reverse = (90 < absolute_angle);
    const int16_t curve_angle = is_reverse ?
        (int16_t)(180 - absolute_angle) : absolute_angle;
    int16_t left = 0;
    int16_t right = 0;

    mix_forward_right(curve_angle, &left, &right);
    if (is_reverse) {
        const int16_t previous_left = left;
        left = (int16_t)-right;
        right = (int16_t)-previous_left;
    }
    if (is_left) {
        const int16_t previous_left = left;
        left = right;
        right = previous_left;
    }
    *left_logic = left;
    *right_logic = right;
}

static rc_drive_slot_id_t select_slot(uint32_t track_index,
                                      int16_t logic_speed)
{
    if (RC_DRIVE_TRACK_LEFT == track_index) {
        return (0 < logic_speed) ? RC_DRIVE_SLOT_LEFT_FORWARD :
                                   RC_DRIVE_SLOT_LEFT_REVERSE;
    }
    return (0 < logic_speed) ? RC_DRIVE_SLOT_RIGHT_FORWARD :
                               RC_DRIVE_SLOT_RIGHT_REVERSE;
}

static int16_t map_logic_to_pwm(const rc_drive_config_t *config,
                                uint32_t track_index,
                                int16_t logic_speed)
{
    int16_t absolute_logic = logic_speed;
    int16_t duty_pct = 0;
    rc_drive_slot_id_t slot = RC_DRIVE_SLOT_LEFT_FORWARD;

    if (0 == logic_speed) {
        return 0;
    }
    if (0 > absolute_logic) {
        absolute_logic = (int16_t)-absolute_logic;
    }
    duty_pct = (int16_t)(RC_DRIVE_PWM_NONZERO_MIN +
        divide_round_away_from_zero(
            (int32_t)(absolute_logic - 1) *
                (RC_DRIVE_PWM_MAX - RC_DRIVE_PWM_NONZERO_MIN),
            RC_DRIVE_LOGIC_MAX - 1));
    slot = select_slot(track_index, logic_speed);
    duty_pct = (int16_t)(duty_pct + config->slots[slot].duty_offset_pct);
    if (1 > duty_pct) {
        duty_pct = 1;
    } else if (RC_DRIVE_PWM_MAX < duty_pct) {
        duty_pct = RC_DRIVE_PWM_MAX;
    }
    return (0 < logic_speed) ? duty_pct : (int16_t)-duty_pct;
}

static bool config_is_valid(const rc_drive_config_t *config)
{
    uint32_t slot_index = 0U;

    for (slot_index = 0U; slot_index < RC_DRIVE_SLOT_COUNT; ++slot_index) {
        const rc_drive_slot_config_t *slot = &config->slots[slot_index];
        if ((RC_DRIVE_DUTY_OFFSET_MIN > slot->duty_offset_pct) ||
            (RC_DRIVE_DUTY_OFFSET_MAX < slot->duty_offset_pct) ||
            (RC_DRIVE_START_DUTY_MIN > slot->start_duty_pct) ||
            (RC_DRIVE_START_DUTY_MAX < slot->start_duty_pct) ||
            (RC_DRIVE_START_BOOST_MIN_MS > slot->start_boost_ms) ||
            (RC_DRIVE_START_BOOST_MAX_MS < slot->start_boost_ms) ||
            (0U != (slot->start_boost_ms % RC_DRIVE_STEP_MS))) {
            return false;
        }
    }
    return true;
}

static int16_t absolute_value(int16_t value)
{
    return (0 > value) ? (int16_t)-value : value;
}

static void step_track(rc_drive_controller_t *controller,
                       uint32_t track_index)
{
    rc_drive_track_state_t *track = &controller->tracks[track_index];
    int16_t target_pwm = 0;
    rc_drive_slot_id_t slot = RC_DRIVE_SLOT_LEFT_FORWARD;

    if (track->ramp_from_reversal) {
        int16_t next_magnitude = 0;
        const int16_t target_magnitude = absolute_value(track->target_logic);
        const bool starts_reverse_output = (0 == track->current_logic);
        const bool changes_direction_again =
            ((0 < track->current_logic) && (0 > track->target_logic)) ||
            ((0 > track->current_logic) && (0 < track->target_logic));

        if (0 == track->target_logic) {
            track->ramp_from_reversal = false;
            track->reversal_ticks_remaining = 0U;
            track->current_logic = 0;
            track->output_pwm_pct = 0;
            return;
        }
        if (changes_direction_again) {
            track->current_logic = 0;
            track->output_pwm_pct = 0;
            track->reversal_ticks_remaining = (uint8_t)(
                RC_DRIVE_REVERSAL_DEAD_MS / RC_DRIVE_STEP_MS);
            return;
        }
        if (0U < track->reversal_ticks_remaining) {
            --track->reversal_ticks_remaining;
            if (0U < track->reversal_ticks_remaining) {
                track->current_logic = 0;
                track->output_pwm_pct = 0;
                return;
            }
        }
        next_magnitude = (int16_t)(absolute_value(track->current_logic) +
                                   RC_DRIVE_ACCEL_STEP_LOGIC);
        if (target_magnitude < next_magnitude) {
            next_magnitude = target_magnitude;
        }
        track->current_logic = (0 > track->target_logic) ?
            (int16_t)-next_magnitude : next_magnitude;
        target_pwm = map_logic_to_pwm(&controller->config,
                                      track_index,
                                      track->current_logic);
        slot = select_slot(track_index, track->current_logic);
        if (starts_reverse_output &&
            ((uint16_t)absolute_value(target_pwm) <
             controller->config.slots[slot].start_duty_pct)) {
            track->boost_ticks_remaining = (uint8_t)(
                controller->config.slots[slot].start_boost_ms /
                RC_DRIVE_STEP_MS);
            track->output_pwm_pct = (0 < target_pwm) ?
                controller->config.slots[slot].start_duty_pct :
                (int16_t)-controller->config.slots[slot].start_duty_pct;
            --track->boost_ticks_remaining;
        } else {
            track->output_pwm_pct = target_pwm;
        }
        if (target_magnitude == next_magnitude) {
            track->ramp_from_reversal = false;
        }
        return;
    }

    if (0U < track->boost_ticks_remaining) {
        const bool changes_direction =
            ((0 < track->current_logic) && (0 > track->target_logic)) ||
            ((0 > track->current_logic) && (0 < track->target_logic));

        if (changes_direction) {
            track->boost_ticks_remaining = 0U;
            track->current_logic = 0;
            track->output_pwm_pct = 0;
            track->reversal_ticks_remaining = (uint8_t)(
                RC_DRIVE_REVERSAL_DEAD_MS / RC_DRIVE_STEP_MS);
            track->ramp_from_reversal = true;
            return;
        }
        if (0 == track->target_logic) {
            track->boost_ticks_remaining = 0U;
        } else {
            track->current_logic = track->target_logic;
            target_pwm = map_logic_to_pwm(&controller->config, track_index,
                                          track->current_logic);
            slot = select_slot(track_index, track->current_logic);
            if (controller->config.slots[slot].start_duty_pct <=
                (uint16_t)absolute_value(target_pwm)) {
                track->boost_ticks_remaining = 0U;
                track->output_pwm_pct = target_pwm;
                return;
            }
            track->output_pwm_pct = (0 < target_pwm) ?
                controller->config.slots[slot].start_duty_pct :
                (int16_t)-controller->config.slots[slot].start_duty_pct;
            --track->boost_ticks_remaining;
            return;
        }
    }

    if ((0 == track->current_logic) && (0 != track->target_logic)) {
        track->current_logic = track->target_logic;
        target_pwm = map_logic_to_pwm(&controller->config, track_index,
                                      track->current_logic);
        slot = select_slot(track_index, track->current_logic);
        if ((uint16_t)absolute_value(target_pwm) <
            controller->config.slots[slot].start_duty_pct) {
            track->boost_ticks_remaining = (uint8_t)(
                controller->config.slots[slot].start_boost_ms /
                RC_DRIVE_STEP_MS);
            track->output_pwm_pct = (0 < target_pwm) ?
                controller->config.slots[slot].start_duty_pct :
                (int16_t)-controller->config.slots[slot].start_duty_pct;
            --track->boost_ticks_remaining;
            return;
        }
        track->output_pwm_pct = target_pwm;
        return;
    }

    if (0 != track->current_logic) {
        const bool same_direction =
            ((0 < track->current_logic) && (0 < track->target_logic)) ||
            ((0 > track->current_logic) && (0 > track->target_logic));
        if ((0 == track->target_logic) || same_direction) {
            const int16_t current_magnitude =
                absolute_value(track->current_logic);
            const int16_t target_magnitude =
                absolute_value(track->target_logic);
            int16_t next_magnitude = current_magnitude;

            if (current_magnitude < target_magnitude) {
                next_magnitude = (int16_t)(current_magnitude +
                                           RC_DRIVE_ACCEL_STEP_LOGIC);
                if (target_magnitude < next_magnitude) {
                    next_magnitude = target_magnitude;
                }
            } else if (current_magnitude > target_magnitude) {
                next_magnitude = (int16_t)(current_magnitude -
                                           RC_DRIVE_DECEL_STEP_LOGIC);
                if ((0 > next_magnitude) ||
                    (target_magnitude > next_magnitude)) {
                    next_magnitude = target_magnitude;
                }
            }
            track->current_logic = (0 > track->current_logic) ?
                (int16_t)-next_magnitude : next_magnitude;
            track->output_pwm_pct = map_logic_to_pwm(&controller->config,
                                                      track_index,
                                                      track->current_logic);
            return;
        }

        track->boost_ticks_remaining = 0U;
        track->current_logic = 0;
        track->output_pwm_pct = 0;
        track->reversal_ticks_remaining = (uint8_t)(
            RC_DRIVE_REVERSAL_DEAD_MS / RC_DRIVE_STEP_MS);
        track->ramp_from_reversal = true;
        return;
    }

    track->current_logic = track->target_logic;
    track->output_pwm_pct = map_logic_to_pwm(&controller->config,
                                              track_index,
                                              track->current_logic);
}

void rc_drive_config_set_defaults(rc_drive_config_t *config)
{
    uint32_t slot_index = 0U;

    if (NULL == config) {
        return;
    }
    for (slot_index = 0U; slot_index < RC_DRIVE_SLOT_COUNT; ++slot_index) {
        config->slots[slot_index].duty_offset_pct = 0;
        config->slots[slot_index].start_duty_pct =
            RC_DRIVE_DEFAULT_START_DUTY_PCT;
        config->slots[slot_index].start_boost_ms =
            RC_DRIVE_DEFAULT_START_BOOST_MS;
    }
}

bool rc_drive_controller_init(rc_drive_controller_t *controller,
                              const rc_drive_config_t *config)
{
    if (NULL == controller) {
        return false;
    }
    *controller = (rc_drive_controller_t){0};
    if ((NULL == config) || !config_is_valid(config)) {
        return false;
    }
    controller->config = *config;
    controller->initialized = true;
    return true;
}

void rc_drive_controller_set_target(rc_drive_controller_t *controller,
                                    const rc_ctrl_command_t *command)
{
    if ((NULL == controller) || !controller->initialized ||
        (NULL == command)) {
        return;
    }
    if (RC_CTRL_MODE_DRIVE == command->mode) {
        int16_t full_left = 0;
        int16_t full_right = 0;
        mix_full_magnitude(command->angle_deg, &full_left, &full_right);
        controller->tracks[RC_DRIVE_TRACK_LEFT].target_logic =
            divide_round_away_from_zero(
            (int32_t)full_left * command->magnitude_pct,
            RC_CTRL_MAGNITUDE_MAX);
        controller->tracks[RC_DRIVE_TRACK_RIGHT].target_logic =
            divide_round_away_from_zero(
            (int32_t)full_right * command->magnitude_pct,
            RC_CTRL_MAGNITUDE_MAX);
    } else {
        controller->tracks[RC_DRIVE_TRACK_LEFT].target_logic = 0;
        controller->tracks[RC_DRIVE_TRACK_RIGHT].target_logic = 0;
    }
}

bool rc_drive_controller_step(rc_drive_controller_t *controller,
                              rc_drive_output_t *output)
{
    bool changed = false;
    int16_t previous_left_logic = 0;
    int16_t previous_right_logic = 0;
    int16_t previous_left_pwm = 0;
    int16_t previous_right_pwm = 0;

    if ((NULL == controller) || !controller->initialized ||
        (NULL == output)) {
        return false;
    }

    previous_left_logic =
        controller->tracks[RC_DRIVE_TRACK_LEFT].current_logic;
    previous_right_logic =
        controller->tracks[RC_DRIVE_TRACK_RIGHT].current_logic;
    previous_left_pwm =
        controller->tracks[RC_DRIVE_TRACK_LEFT].output_pwm_pct;
    previous_right_pwm =
        controller->tracks[RC_DRIVE_TRACK_RIGHT].output_pwm_pct;

    step_track(controller, RC_DRIVE_TRACK_LEFT);
    step_track(controller, RC_DRIVE_TRACK_RIGHT);
    changed = (previous_left_logic !=
               controller->tracks[RC_DRIVE_TRACK_LEFT].current_logic) ||
              (previous_right_logic !=
               controller->tracks[RC_DRIVE_TRACK_RIGHT].current_logic) ||
              (previous_left_pwm !=
               controller->tracks[RC_DRIVE_TRACK_LEFT].output_pwm_pct) ||
              (previous_right_pwm !=
               controller->tracks[RC_DRIVE_TRACK_RIGHT].output_pwm_pct);

    output->left_logic =
        controller->tracks[RC_DRIVE_TRACK_LEFT].current_logic;
    output->right_logic =
        controller->tracks[RC_DRIVE_TRACK_RIGHT].current_logic;
    output->left_pwm_pct =
        controller->tracks[RC_DRIVE_TRACK_LEFT].output_pwm_pct;
    output->right_pwm_pct =
        controller->tracks[RC_DRIVE_TRACK_RIGHT].output_pwm_pct;
    return changed;
}

void rc_drive_controller_stop(rc_drive_controller_t *controller,
                              rc_drive_output_t *output)
{
    if ((NULL == controller) || (NULL == output)) {
        return;
    }
    controller->tracks[RC_DRIVE_TRACK_LEFT] = (rc_drive_track_state_t){0};
    controller->tracks[RC_DRIVE_TRACK_RIGHT] = (rc_drive_track_state_t){0};
    *output = (rc_drive_output_t){0};
}
