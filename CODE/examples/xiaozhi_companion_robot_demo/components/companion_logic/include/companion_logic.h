#pragma once

#include "companion_core.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COMPANION_ACTION_STOP = 0,
    COMPANION_ACTION_FORWARD,
    COMPANION_ACTION_BACKWARD,
    COMPANION_ACTION_TURN_LEFT,
    COMPANION_ACTION_TURN_RIGHT,
    COMPANION_ACTION_UTURN_LEFT,
    COMPANION_ACTION_UTURN_RIGHT,
    COMPANION_ACTION_COUNT,
} companion_action_t;

typedef struct {
    bool enabled;
    uint16_t weight;
    uint32_t min_duration_ms;
    uint32_t max_duration_ms;
} companion_action_config_t;

typedef struct {
    companion_action_config_t actions[COMPANION_ACTION_COUNT];
    uint32_t min_stop_ms;
    uint32_t max_stop_ms;
    uint32_t initial_delay_ms;
} companion_roam_config_t;

typedef struct {
    companion_action_t action;
    uint32_t duration_ms;
    uint32_t stop_ms;
} companion_action_plan_t;

typedef struct {
    companion_turn_direction_t direction;
    uint32_t duration_ms;
    float relative_deg;
} companion_turn_plan_t;

typedef enum {
    COMPANION_EXPRESSION_BOOT = 0,
    COMPANION_EXPRESSION_SMILE,
    COMPANION_EXPRESSION_LOCATE,
    COMPANION_EXPRESSION_LOOK_LEFT,
    COMPANION_EXPRESSION_LOOK_RIGHT,
    COMPANION_EXPRESSION_LISTEN,
    COMPANION_EXPRESSION_THINK,
    COMPANION_EXPRESSION_TALK,
    COMPANION_EXPRESSION_ERROR,
} companion_expression_base_t;

typedef enum {
    COMPANION_EXPRESSION_EFFECT_NONE = 0,
    COMPANION_EXPRESSION_EFFECT_BLINK,
    COMPANION_EXPRESSION_EFFECT_MOUTH_OPEN,
    COMPANION_EXPRESSION_EFFECT_MOUTH_CLOSED,
    COMPANION_EXPRESSION_EFFECT_POUT_IN,
    COMPANION_EXPRESSION_EFFECT_POUT_OUT,
} companion_expression_effect_t;

typedef struct {
    companion_expression_base_t base;
    companion_expression_effect_t effect;
    bool touch_active;
    uint32_t revision;
} companion_expression_snapshot_t;

typedef struct {
    companion_product_state_t product_state;
    companion_expression_snapshot_t last_snapshot;
    bool touch_active;
    bool initialized;
} companion_expression_model_t;

void companion_roam_config_default(companion_roam_config_t *config);
esp_err_t companion_roam_config_validate(const companion_roam_config_t *config);
esp_err_t companion_behavior_plan(const companion_roam_config_t *config,
                                  uint32_t action_random,
                                  uint32_t duration_random,
                                  uint32_t stop_random,
                                  companion_action_plan_t *plan);
esp_err_t companion_action_tracks(companion_action_t action, int *left_percent,
                                  int *right_percent);
esp_err_t companion_turn_plan_from_relative(float relative_deg,
                                            companion_turn_plan_t *plan);

void companion_expression_init(companion_expression_model_t *model);
esp_err_t companion_expression_set_product(companion_expression_model_t *model,
                                           companion_product_state_t state);
esp_err_t companion_expression_set_touch(companion_expression_model_t *model,
                                         bool active);
esp_err_t companion_expression_model_render(
    companion_expression_model_t *model,
    uint64_t now_ms,
    uint32_t random_value,
    companion_expression_snapshot_t *snapshot);
