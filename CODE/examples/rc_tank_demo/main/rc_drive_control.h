#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rc_ctrl_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC_DRIVE_TRACK_COUNT 2U
#define RC_DRIVE_SLOT_COUNT  4U
#define RC_DRIVE_DEFAULT_START_DUTY_PCT 60U
#define RC_DRIVE_DEFAULT_START_BOOST_MS 60U
#define RC_DRIVE_LOGIC_MAX 100
#define RC_DRIVE_PWM_NONZERO_MIN 50
#define RC_DRIVE_PWM_MAX 100
#define RC_DRIVE_DUTY_OFFSET_MIN (-20)
#define RC_DRIVE_DUTY_OFFSET_MAX 20
#define RC_DRIVE_START_DUTY_MIN 1U
#define RC_DRIVE_START_DUTY_MAX 100U
#define RC_DRIVE_START_BOOST_MIN_MS 20U
#define RC_DRIVE_START_BOOST_MAX_MS 200U
#define RC_DRIVE_STEP_MS 20U
#define RC_DRIVE_ACCEL_STEP_LOGIC 20
#define RC_DRIVE_DECEL_STEP_LOGIC 25
#define RC_DRIVE_REVERSAL_DEAD_MS 40U

typedef enum {
    RC_DRIVE_SLOT_LEFT_FORWARD = 0,
    RC_DRIVE_SLOT_LEFT_REVERSE = 1,
    RC_DRIVE_SLOT_RIGHT_FORWARD = 2,
    RC_DRIVE_SLOT_RIGHT_REVERSE = 3,
} rc_drive_slot_id_t;

typedef enum {
    RC_DRIVE_TRACK_LEFT = 0,
    RC_DRIVE_TRACK_RIGHT = 1,
} rc_drive_track_id_t;

typedef struct {
    int8_t duty_offset_pct;
    uint8_t start_duty_pct;
    uint16_t start_boost_ms;
} rc_drive_slot_config_t;

typedef struct {
    rc_drive_slot_config_t slots[RC_DRIVE_SLOT_COUNT];
} rc_drive_config_t;

typedef struct {
    int16_t target_logic;
    int16_t current_logic;
    int16_t output_pwm_pct;
    uint8_t boost_ticks_remaining;
    uint8_t reversal_ticks_remaining;
    bool ramp_from_reversal;
} rc_drive_track_state_t;

typedef struct {
    rc_drive_config_t config;
    rc_drive_track_state_t tracks[RC_DRIVE_TRACK_COUNT];
    bool initialized;
} rc_drive_controller_t;

typedef struct {
    int16_t left_logic;
    int16_t right_logic;
    int16_t left_pwm_pct;
    int16_t right_pwm_pct;
} rc_drive_output_t;

void rc_drive_config_set_defaults(rc_drive_config_t *config);
bool rc_drive_controller_init(rc_drive_controller_t *controller,
                              const rc_drive_config_t *config);
void rc_drive_controller_set_target(rc_drive_controller_t *controller,
                                    const rc_ctrl_command_t *command);
bool rc_drive_controller_step(rc_drive_controller_t *controller,
                              rc_drive_output_t *output);
void rc_drive_controller_stop(rc_drive_controller_t *controller,
                              rc_drive_output_t *output);

#ifdef __cplusplus
}
#endif
