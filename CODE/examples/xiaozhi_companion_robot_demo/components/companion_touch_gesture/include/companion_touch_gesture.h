#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COMPANION_TOUCH_TRANSITION_NONE = 0,
    COMPANION_TOUCH_TRANSITION_PRESSED,
    COMPANION_TOUCH_TRANSITION_RELEASED,
} companion_touch_transition_t;

typedef enum {
    COMPANION_GESTURE_PACK_NONE = 0,
    COMPANION_GESTURE_PACK_PREVIOUS,
    COMPANION_GESTURE_PACK_NEXT,
} companion_gesture_pack_step_t;

typedef enum {
    COMPANION_GESTURE_CONTACT_IDLE = 0,
    COMPANION_GESTURE_CONTACT_DEBOUNCE,
    COMPANION_GESTURE_CONTACT_PENDING,
    COMPANION_GESTURE_CONTACT_SWIPE_PENDING,
    COMPANION_GESTURE_CONTACT_TOUCH_ACTIVE,
    COMPANION_GESTURE_CONTACT_QUICK_TAP,
} companion_gesture_contact_state_t;

typedef struct {
    uint16_t display_width;
    uint16_t display_height;
    uint32_t press_debounce_ms;
    uint32_t touch_decision_ms;
    uint32_t release_debounce_ms;
    uint32_t tap_feedback_ms;
    uint16_t swipe_intent_horizontal_px;
    uint16_t swipe_min_horizontal_px;
    uint16_t swipe_max_vertical_px;
    uint32_t swipe_max_duration_ms;
} companion_touch_gesture_config_t;

typedef struct {
    companion_touch_transition_t touch_transition;
    companion_gesture_pack_step_t pack_step;
    int16_t delta_x;
    int16_t delta_y;
    uint32_t duration_ms;
    bool synthetic_feedback;
} companion_touch_gesture_result_t;

typedef struct {
    companion_touch_gesture_config_t config;
    uint64_t contact_start_ms;
    uint64_t release_start_ms;
    uint16_t start_x;
    uint16_t start_y;
    uint16_t last_x;
    uint16_t last_y;
    companion_gesture_contact_state_t contact_state;
    bool contact_active;
    bool release_pending;
    bool swipe_emitted;
    bool swipe_disqualified;
    bool initialized;
} companion_touch_gesture_t;

esp_err_t companion_touch_gesture_init(
    companion_touch_gesture_t *gesture,
    const companion_touch_gesture_config_t *config);
esp_err_t companion_touch_gesture_update(
    companion_touch_gesture_t *gesture,
    bool raw_touch,
    uint16_t display_x,
    uint16_t display_y,
    uint64_t now_ms,
    companion_touch_gesture_result_t *result);
