#pragma once

#include "companion_core.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*companion_ui_touch_cb_t)(bool pressed, void *user_ctx);
typedef void (*companion_ui_error_cb_t)(esp_err_t error, void *user_ctx);
typedef void (*companion_ui_touch_health_cb_t)(bool available,
                                               esp_err_t error,
                                               void *user_ctx);

typedef struct {
    uint32_t touch_press_debounce_ms;
    uint32_t touch_decision_ms;
    uint32_t touch_release_debounce_ms;
    uint32_t touch_tap_feedback_ms;
    uint16_t swipe_intent_horizontal_px;
    uint16_t swipe_min_horizontal_px;
    uint16_t swipe_max_vertical_px;
    uint32_t swipe_max_duration_ms;
    companion_ui_touch_cb_t on_touch;
    companion_ui_error_cb_t on_error;
    companion_ui_touch_health_cb_t on_touch_health;
    void *user_ctx;
} companion_ui_config_t;

typedef struct {
    companion_product_state_t product_state;
    companion_turn_direction_t look_direction;
    bool roam_enabled;
    bool network_ready;
    bool doa_debug_valid;
    int16_t doa_remaining_deg;
    uint32_t generation;
    uint32_t wake_seq;
    bool merit_bubble_active;
    uint32_t merit_bubble_epoch;
    uint64_t merit_bubble_start_ms;
    uint32_t merit_bubble_repeat_count;
} companion_ui_state_t;

esp_err_t companion_ui_start(const companion_ui_config_t *config);
esp_err_t companion_ui_set_state(const companion_ui_state_t *state);
bool companion_ui_is_available(void);
