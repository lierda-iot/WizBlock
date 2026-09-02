#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef esp_err_t (*pt2466_motor_set_input_fn)(void *ctx, uint8_t input_index, uint16_t duty_permille);

typedef struct {
    const char *name;
    uint8_t positive_input;
    uint8_t negative_input;
    bool invert_direction;
    pt2466_motor_set_input_fn set_input;
    void *user_ctx;
} pt2466_motor_config_t;

typedef struct {
    pt2466_motor_config_t config;
    int last_speed_percent;
    bool initialized;
} pt2466_motor_t;

esp_err_t pt2466_motor_init(pt2466_motor_t *motor, const pt2466_motor_config_t *config);
esp_err_t pt2466_motor_set_speed(pt2466_motor_t *motor, int speed_percent);
esp_err_t pt2466_motor_stop(pt2466_motor_t *motor);
