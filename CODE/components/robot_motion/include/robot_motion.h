#pragma once

#include "esp_err.h"
#include "pt2466_motor.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    pt2466_motor_t *left;
    pt2466_motor_t *right;
    bool initialized;
} robot_motion_t;

typedef struct {
    int speed_percent;
    uint32_t move_ms;
    uint32_t stop_ms;
} robot_motion_smoke_test_config_t;

esp_err_t robot_motion_init(robot_motion_t *motion, pt2466_motor_t *left, pt2466_motor_t *right);
esp_err_t robot_motion_set_track_speed(robot_motion_t *motion, int left_percent, int right_percent);
esp_err_t robot_motion_stop(robot_motion_t *motion);
esp_err_t robot_motion_forward(robot_motion_t *motion, int speed_percent);
esp_err_t robot_motion_backward(robot_motion_t *motion, int speed_percent);
esp_err_t robot_motion_turn_left(robot_motion_t *motion, int speed_percent);
esp_err_t robot_motion_turn_right(robot_motion_t *motion, int speed_percent);
esp_err_t robot_motion_run_smoke_test(robot_motion_t *motion, const robot_motion_smoke_test_config_t *config);
