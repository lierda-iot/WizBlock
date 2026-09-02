#pragma once

#include "companion_core.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMPANION_EXPRESSION_SURFACE_WIDTH 320U
#define COMPANION_EXPRESSION_SURFACE_HEIGHT 240U

typedef struct companion_expression companion_expression_t;

typedef struct {
    bool active;
    uint32_t epoch;
    uint64_t start_ms;
} companion_merit_bubble_signal_t;

typedef struct {
    uint32_t blink_interval_min_ms;
    uint32_t blink_interval_max_ms;
    uint32_t blink_closed_ms;
    uint32_t blink_double_gap_ms;
    uint8_t blink_double_percent;
    uint32_t mouth_open_min_ms;
    uint32_t mouth_open_max_ms;
    uint32_t mouth_closed_min_ms;
    uint32_t mouth_closed_max_ms;
    uint32_t pout_compress_ms;
    uint32_t pout_expand_ms;
    uint16_t saturation_gain_percent;
    uint8_t saturation_limit_percent;
    uint16_t render_scale_percent;
    int16_t render_offset_x_px;
    int16_t render_offset_y_px;
} companion_expression_config_t;

typedef struct {
    companion_product_state_t product_state;
    companion_turn_direction_t turn_direction;
    bool touch_active;
    companion_merit_bubble_signal_t merit_bubble;
} companion_expression_signals_t;

typedef enum {
    COMPANION_PACK_PREVIOUS = -1,
    COMPANION_PACK_NEXT = 1,
} companion_pack_step_t;

typedef struct {
    uint16_t *pixels;
    size_t width;
    size_t height;
    size_t stride_pixels;
} companion_rgb565_surface_t;

typedef struct {
    bool changed;
    uint32_t revision;
    const char *current_pack_id;
} companion_expression_result_t;

void companion_expression_config_default(companion_expression_config_t *config);
esp_err_t companion_expression_open(
    const companion_expression_config_t *config,
    companion_expression_t **expression);
void companion_expression_close(companion_expression_t *expression);
size_t companion_expression_pack_count(
    const companion_expression_t *expression);
esp_err_t companion_expression_step_pack(
    companion_expression_t *expression,
    companion_pack_step_t step);
esp_err_t companion_expression_render(
    companion_expression_t *expression,
    const companion_expression_signals_t *signals,
    uint64_t now_ms,
    uint32_t random_value,
    companion_rgb565_surface_t *surface,
    companion_expression_result_t *result);
