#include "companion_touch_gesture.h"

#include <limits.h>
#include <string.h>

static uint16_t absolute_delta(uint16_t first, uint16_t second)
{
    return (first >= second) ? (first - second) : (second - first);
}

static int16_t signed_delta(uint16_t start, uint16_t end)
{
    const int32_t delta = (int32_t)end - (int32_t)start;
    if (delta < INT16_MIN) {
        return INT16_MIN;
    }
    if (INT16_MAX < delta) {
        return INT16_MAX;
    }
    return (int16_t)delta;
}

static void reset_contact(companion_touch_gesture_t *gesture)
{
    gesture->contact_start_ms = 0U;
    gesture->release_start_ms = 0U;
    gesture->start_x = 0U;
    gesture->start_y = 0U;
    gesture->last_x = 0U;
    gesture->last_y = 0U;
    gesture->contact_state = COMPANION_GESTURE_CONTACT_IDLE;
    gesture->contact_active = false;
    gesture->release_pending = false;
    gesture->swipe_emitted = false;
    gesture->swipe_disqualified = false;
}

static void set_result_motion(companion_touch_gesture_result_t *result,
                              const companion_touch_gesture_t *gesture,
                              uint64_t elapsed_ms)
{
    result->delta_x = signed_delta(gesture->start_x, gesture->last_x);
    result->delta_y = signed_delta(gesture->start_y, gesture->last_y);
    result->duration_ms = (UINT32_MAX < elapsed_ms) ? UINT32_MAX :
                          (uint32_t)elapsed_ms;
}

esp_err_t companion_touch_gesture_init(
    companion_touch_gesture_t *gesture,
    const companion_touch_gesture_config_t *config)
{
    if (NULL == gesture || NULL == config ||
        0U == config->display_width || 0U == config->display_height ||
        0U == config->press_debounce_ms ||
        config->touch_decision_ms < config->press_debounce_ms ||
        0U == config->release_debounce_ms ||
        0U == config->tap_feedback_ms ||
        0U == config->swipe_intent_horizontal_px ||
        0U == config->swipe_min_horizontal_px ||
        config->swipe_min_horizontal_px <=
            config->swipe_intent_horizontal_px ||
        config->display_width <= config->swipe_min_horizontal_px ||
        config->display_height <= config->swipe_max_vertical_px ||
        config->swipe_max_duration_ms < config->touch_decision_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(gesture, 0, sizeof(*gesture));
    gesture->config = *config;
    gesture->initialized = true;
    return ESP_OK;
}

esp_err_t companion_touch_gesture_update(
    companion_touch_gesture_t *gesture,
    bool raw_touch,
    uint16_t display_x,
    uint16_t display_y,
    uint64_t now_ms,
    companion_touch_gesture_result_t *result)
{
    if (NULL == gesture || !gesture->initialized || NULL == result ||
        (raw_touch &&
         (gesture->config.display_width <= display_x ||
          gesture->config.display_height <= display_y))) {
        return ESP_ERR_INVALID_ARG;
    }
    *result = (companion_touch_gesture_result_t) {0};

    if (COMPANION_GESTURE_CONTACT_QUICK_TAP == gesture->contact_state) {
        if (now_ms < gesture->contact_start_ms) {
            return ESP_ERR_INVALID_STATE;
        }
        const uint64_t feedback_ms = now_ms - gesture->contact_start_ms;
        if (raw_touch || gesture->config.tap_feedback_ms <= feedback_ms) {
            result->touch_transition = COMPANION_TOUCH_TRANSITION_RELEASED;
            result->synthetic_feedback = true;
            set_result_motion(result, gesture, feedback_ms);
            reset_contact(gesture);
        }
        if (!raw_touch) {
            return ESP_OK;
        }
    }

    if (raw_touch) {
        if (!gesture->contact_active) {
            reset_contact(gesture);
            gesture->contact_active = true;
            gesture->contact_state = COMPANION_GESTURE_CONTACT_DEBOUNCE;
            gesture->contact_start_ms = now_ms;
            gesture->start_x = display_x;
            gesture->start_y = display_y;
            gesture->last_x = display_x;
            gesture->last_y = display_y;
        } else if (now_ms < gesture->contact_start_ms) {
            return ESP_ERR_INVALID_STATE;
        }

        gesture->release_pending = false;
        gesture->release_start_ms = 0U;
        gesture->last_x = display_x;
        gesture->last_y = display_y;
        const uint64_t elapsed_ms = now_ms - gesture->contact_start_ms;
        const uint16_t horizontal = absolute_delta(gesture->start_x, display_x);
        const uint16_t vertical = absolute_delta(gesture->start_y, display_y);

        if (COMPANION_GESTURE_CONTACT_TOUCH_ACTIVE ==
            gesture->contact_state) {
            return ESP_OK;
        }
        if (COMPANION_GESTURE_CONTACT_DEBOUNCE == gesture->contact_state &&
            gesture->config.press_debounce_ms <= elapsed_ms) {
            gesture->contact_state = COMPANION_GESTURE_CONTACT_PENDING;
        }
        if (gesture->config.swipe_max_duration_ms < elapsed_ms ||
            gesture->config.swipe_max_vertical_px < vertical) {
            gesture->swipe_disqualified = true;
        }
        if (COMPANION_GESTURE_CONTACT_PENDING == gesture->contact_state &&
            !gesture->swipe_disqualified && horizontal > vertical &&
            gesture->config.swipe_intent_horizontal_px <= horizontal) {
            gesture->contact_state =
                COMPANION_GESTURE_CONTACT_SWIPE_PENDING;
        }
        if (COMPANION_GESTURE_CONTACT_SWIPE_PENDING ==
                gesture->contact_state &&
            !gesture->swipe_emitted &&
            gesture->config.swipe_max_duration_ms >= elapsed_ms &&
            gesture->config.swipe_max_vertical_px >= vertical &&
            gesture->config.swipe_min_horizontal_px <= horizontal) {
            gesture->swipe_emitted = true;
            result->pack_step = (display_x < gesture->start_x) ?
                COMPANION_GESTURE_PACK_NEXT :
                COMPANION_GESTURE_PACK_PREVIOUS;
            set_result_motion(result, gesture, elapsed_ms);
            return ESP_OK;
        }
        if (COMPANION_GESTURE_CONTACT_PENDING == gesture->contact_state &&
            gesture->config.touch_decision_ms <= elapsed_ms) {
            gesture->contact_state = COMPANION_GESTURE_CONTACT_TOUCH_ACTIVE;
            result->touch_transition = COMPANION_TOUCH_TRANSITION_PRESSED;
            set_result_motion(result, gesture, elapsed_ms);
        }
        return ESP_OK;
    }

    if (!gesture->contact_active) {
        return ESP_OK;
    }
    if (now_ms < gesture->contact_start_ms) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint64_t elapsed_ms = now_ms - gesture->contact_start_ms;
    if (COMPANION_GESTURE_CONTACT_DEBOUNCE == gesture->contact_state &&
        gesture->config.press_debounce_ms > elapsed_ms) {
        reset_contact(gesture);
        return ESP_OK;
    }
    if (COMPANION_GESTURE_CONTACT_DEBOUNCE == gesture->contact_state ||
        COMPANION_GESTURE_CONTACT_PENDING == gesture->contact_state) {
        gesture->contact_state = COMPANION_GESTURE_CONTACT_QUICK_TAP;
        gesture->contact_start_ms = now_ms;
        result->touch_transition = COMPANION_TOUCH_TRANSITION_PRESSED;
        result->synthetic_feedback = true;
        set_result_motion(result, gesture, elapsed_ms);
        return ESP_OK;
    }
    if (COMPANION_GESTURE_CONTACT_SWIPE_PENDING == gesture->contact_state) {
        reset_contact(gesture);
        return ESP_OK;
    }
    if (!gesture->release_pending) {
        gesture->release_pending = true;
        gesture->release_start_ms = now_ms;
        return ESP_OK;
    }
    if (now_ms < gesture->release_start_ms) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint64_t release_ms = now_ms - gesture->release_start_ms;
    if (gesture->config.release_debounce_ms <= release_ms) {
        result->touch_transition = COMPANION_TOUCH_TRANSITION_RELEASED;
        set_result_motion(result, gesture, elapsed_ms);
        reset_contact(gesture);
    }
    return ESP_OK;
}
