#include "companion_logic.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define COMPANION_TURN_DEAD_ZONE_DEG 10.0f
#define COMPANION_TURN_BOUNDARY_EPSILON_DEG 0.001f
#define COMPANION_TURN_MAX_DEG 90.0f
#define COMPANION_TURN_RATE_DPS 12.0f
#define COMPANION_TURN_TIMEOUT_MARGIN_MS 500U
#define COMPANION_TURN_MIN_TIMEOUT_MS 1500U
#define COMPANION_TURN_MAX_TIMEOUT_MS 8000U
#define COMPANION_TURN_ROUND_MS 10U
#define COMPANION_POUT_IN_MS 220U
#define COMPANION_POUT_OUT_MS 260U
#define COMPANION_BLINK_RANDOM_DIVISOR 20U
#define COMPANION_MOUTH_OPEN_PERCENT 55U

static uint32_t value_inclusive(uint32_t random_value, uint32_t min_value,
                                uint32_t max_value)
{
    const uint32_t span = max_value - min_value;
    if (UINT32_MAX == span) {
        return random_value;
    }
    return min_value + (random_value % (span + 1U));
}

void companion_roam_config_default(companion_roam_config_t *config)
{
    if (NULL == config) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->actions[COMPANION_ACTION_FORWARD] =
        (companion_action_config_t){true, 25U, 300U, 800U};
    config->actions[COMPANION_ACTION_BACKWARD] =
        (companion_action_config_t){true, 15U, 300U, 800U};
    config->actions[COMPANION_ACTION_TURN_LEFT] =
        (companion_action_config_t){true, 15U, 250U, 650U};
    config->actions[COMPANION_ACTION_TURN_RIGHT] =
        (companion_action_config_t){true, 15U, 250U, 650U};
    config->actions[COMPANION_ACTION_UTURN_LEFT] =
        (companion_action_config_t){true, 10U, 700U, 1200U};
    config->actions[COMPANION_ACTION_UTURN_RIGHT] =
        (companion_action_config_t){true, 10U, 700U, 1200U};
    config->min_stop_ms = 3000U;
    config->max_stop_ms = 8000U;
    config->initial_delay_ms = 3000U;
}

esp_err_t companion_roam_config_validate(const companion_roam_config_t *config)
{
    if (NULL == config || 0U == config->min_stop_ms ||
        0U == config->initial_delay_ms ||
        config->min_stop_ms > config->max_stop_ms) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t total_weight = 0U;
    for (int action = COMPANION_ACTION_FORWARD;
         action < COMPANION_ACTION_COUNT; ++action) {
        const companion_action_config_t *item = &config->actions[action];
        if (!item->enabled) {
            continue;
        }
        if (0U == item->weight || 0U == item->min_duration_ms ||
            item->min_duration_ms > item->max_duration_ms) {
            return ESP_ERR_INVALID_ARG;
        }
        if (UINT32_MAX - total_weight < item->weight) {
            return ESP_ERR_INVALID_ARG;
        }
        total_weight += item->weight;
    }
    return (0U == total_weight) ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t companion_behavior_plan(const companion_roam_config_t *config,
                                  uint32_t action_random,
                                  uint32_t duration_random,
                                  uint32_t stop_random,
                                  companion_action_plan_t *plan)
{
    if (NULL == plan || ESP_OK != companion_roam_config_validate(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t total_weight = 0U;
    for (int action = COMPANION_ACTION_FORWARD;
         action < COMPANION_ACTION_COUNT; ++action) {
        if (config->actions[action].enabled) {
            total_weight += config->actions[action].weight;
        }
    }

    uint32_t ticket = action_random % total_weight;
    companion_action_t selected = COMPANION_ACTION_STOP;
    for (int action = COMPANION_ACTION_FORWARD;
         action < COMPANION_ACTION_COUNT; ++action) {
        const companion_action_config_t *item = &config->actions[action];
        if (!item->enabled) {
            continue;
        }
        if (ticket < item->weight) {
            selected = (companion_action_t)action;
            break;
        }
        ticket -= item->weight;
    }

    if (COMPANION_ACTION_STOP == selected) {
        return ESP_FAIL;
    }
    const companion_action_config_t *item = &config->actions[selected];
    plan->action = selected;
    plan->duration_ms = value_inclusive(duration_random,
                                        item->min_duration_ms,
                                        item->max_duration_ms);
    plan->stop_ms = value_inclusive(stop_random, config->min_stop_ms,
                                    config->max_stop_ms);
    return ESP_OK;
}

esp_err_t companion_action_tracks(companion_action_t action, int *left_percent,
                                  int *right_percent)
{
    if (NULL == left_percent || NULL == right_percent) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (action) {
    case COMPANION_ACTION_STOP:
        *left_percent = 0;
        *right_percent = 0;
        return ESP_OK;
    case COMPANION_ACTION_FORWARD:
        *left_percent = 100;
        *right_percent = 100;
        return ESP_OK;
    case COMPANION_ACTION_BACKWARD:
        *left_percent = -100;
        *right_percent = -100;
        return ESP_OK;
    case COMPANION_ACTION_TURN_LEFT:
    case COMPANION_ACTION_UTURN_LEFT:
        *left_percent = -100;
        *right_percent = 100;
        return ESP_OK;
    case COMPANION_ACTION_TURN_RIGHT:
    case COMPANION_ACTION_UTURN_RIGHT:
        *left_percent = 100;
        *right_percent = -100;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t companion_turn_plan_from_relative(float relative_deg,
                                            companion_turn_plan_t *plan)
{
    if (NULL == plan || !isfinite(relative_deg)) {
        return ESP_ERR_INVALID_ARG;
    }

    float limited = relative_deg;
    if (limited > COMPANION_TURN_MAX_DEG) {
        limited = COMPANION_TURN_MAX_DEG;
    } else if (limited < -COMPANION_TURN_MAX_DEG) {
        limited = -COMPANION_TURN_MAX_DEG;
    }

    plan->relative_deg = limited;
    const float magnitude = fabsf(limited);
    if ((magnitude + COMPANION_TURN_BOUNDARY_EPSILON_DEG) <
        COMPANION_TURN_DEAD_ZONE_DEG) {
        plan->direction = COMPANION_TURN_NONE;
        plan->duration_ms = 0U;
        return ESP_OK;
    }

    const float base_timeout_ms = magnitude * 1000.0f /
                                  COMPANION_TURN_RATE_DPS;
    uint32_t rounded = (uint32_t)ceilf(
        base_timeout_ms / (float)COMPANION_TURN_ROUND_MS) *
        COMPANION_TURN_ROUND_MS + COMPANION_TURN_TIMEOUT_MARGIN_MS;
    if (rounded < COMPANION_TURN_MIN_TIMEOUT_MS) {
        rounded = COMPANION_TURN_MIN_TIMEOUT_MS;
    } else if (rounded > COMPANION_TURN_MAX_TIMEOUT_MS) {
        rounded = COMPANION_TURN_MAX_TIMEOUT_MS;
    }
    plan->direction = (0.0f < limited) ? COMPANION_TURN_LEFT :
                                        COMPANION_TURN_RIGHT;
    plan->duration_ms = rounded;
    return ESP_OK;
}

static companion_expression_base_t expression_base(companion_product_state_t state)
{
    switch (state) {
    case COMPANION_PRODUCT_BOOTING:
        return COMPANION_EXPRESSION_BOOT;
    case COMPANION_PRODUCT_WAIT_NETWORK:
        return COMPANION_EXPRESSION_SMILE;
    case COMPANION_PRODUCT_CONNECTING:
        return COMPANION_EXPRESSION_LISTEN;
    case COMPANION_PRODUCT_IDLE:
        return COMPANION_EXPRESSION_SMILE;
    case COMPANION_PRODUCT_LOCATING:
        return COMPANION_EXPRESSION_LOCATE;
    case COMPANION_PRODUCT_TURNING:
        return COMPANION_EXPRESSION_LOCATE;
    case COMPANION_PRODUCT_LISTENING:
        return COMPANION_EXPRESSION_LISTEN;
    case COMPANION_PRODUCT_PROCESSING:
        return COMPANION_EXPRESSION_THINK;
    case COMPANION_PRODUCT_SPEAKING:
        return COMPANION_EXPRESSION_TALK;
    case COMPANION_PRODUCT_ERROR:
    default:
        return COMPANION_EXPRESSION_ERROR;
    }
}

void companion_expression_init(companion_expression_model_t *model)
{
    if (NULL == model) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->product_state = COMPANION_PRODUCT_BOOTING;
}

esp_err_t companion_expression_set_product(companion_expression_model_t *model,
                                           companion_product_state_t state)
{
    if (NULL == model || COMPANION_PRODUCT_STATE_COUNT <= state) {
        return ESP_ERR_INVALID_ARG;
    }
    model->product_state = state;
    return ESP_OK;
}

esp_err_t companion_expression_set_touch(companion_expression_model_t *model,
                                         bool active)
{
    if (NULL == model) {
        return ESP_ERR_INVALID_ARG;
    }
    model->touch_active = active;
    return ESP_OK;
}

esp_err_t companion_expression_model_render(
    companion_expression_model_t *model,
    uint64_t now_ms,
    uint32_t random_value,
    companion_expression_snapshot_t *snapshot)
{
    if (NULL == model || NULL == snapshot) {
        return ESP_ERR_INVALID_ARG;
    }

    companion_expression_snapshot_t next = {
        .base = expression_base(model->product_state),
        .effect = COMPANION_EXPRESSION_EFFECT_NONE,
        .touch_active = model->touch_active,
        .revision = model->last_snapshot.revision,
    };

    if (model->touch_active) {
        const uint64_t period = COMPANION_POUT_IN_MS + COMPANION_POUT_OUT_MS;
        next.effect = ((now_ms % period) < COMPANION_POUT_IN_MS) ?
                      COMPANION_EXPRESSION_EFFECT_POUT_IN :
                      COMPANION_EXPRESSION_EFFECT_POUT_OUT;
    } else if (COMPANION_PRODUCT_SPEAKING == model->product_state) {
        next.effect = ((random_value % 100U) < COMPANION_MOUTH_OPEN_PERCENT) ?
                      COMPANION_EXPRESSION_EFFECT_MOUTH_OPEN :
                      COMPANION_EXPRESSION_EFFECT_MOUTH_CLOSED;
    } else if ((COMPANION_PRODUCT_IDLE == model->product_state ||
                COMPANION_PRODUCT_WAIT_NETWORK == model->product_state) &&
               0U == (random_value % COMPANION_BLINK_RANDOM_DIVISOR)) {
        next.effect = COMPANION_EXPRESSION_EFFECT_BLINK;
    }

    if (!model->initialized || next.base != model->last_snapshot.base ||
        next.effect != model->last_snapshot.effect ||
        next.touch_active != model->last_snapshot.touch_active) {
        next.revision++;
    }
    model->last_snapshot = next;
    model->initialized = true;
    *snapshot = next;
    return ESP_OK;
}
