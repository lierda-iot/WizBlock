#include "audio_spatial_spectrum_ui.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "spatial_spectrum_ui";

#define UI_HOR_RES                  BOARD_LAIWFS300_LCD_V_RES
#define UI_VER_RES                  BOARD_LAIWFS300_LCD_H_RES
#define UI_LCD_PIXEL_CLOCK_HZ       40000000U
#define UI_BUFFER_ROWS              80U
#define UI_TICK_MS                  2U
#define UI_RENDER_INTERVAL_MS       40U
#define UI_TASK_DELAY_MS            5U
#define UI_TASK_STACK               8192U
#define UI_TASK_PRIORITY            2U
#define UI_VISUALIZER_Y             0
#define UI_VISUALIZER_HEIGHT        UI_VER_RES
#define UI_TOUCH_RETRY_COUNT        3U
#define UI_TOUCH_RETRY_DELAY_MS     150U
#define UI_DOA_TRAIL_LENGTH         6U
#define UI_WATERFALL_COLUMNS        40U
#define UI_WATERFALL_ROWS           12U
#define UI_WATERFALL_FRAME_DIVIDER  2U
#define UI_LEVEL_SLOW_ALPHA         0.08f
#define UI_LEVEL_VISIBLE_FLOOR_DBFS -60.0f
#define UI_LEVEL_MIN_DBFS           -120.0f
#define UI_METABALL_COUNT            3U
#define UI_METABALL_DOA_RANGE_PX     36.0f
#define UI_METABALL_DOA_ALPHA        0.12f
#define UI_METABALL_DOA_MAX_STEP_PX  2.0f
#define UI_METABALL_LEVEL_ATTACK     0.15f
#define UI_METABALL_LEVEL_RELEASE    0.08f
#define UI_METABALL_LEVEL_MAX_STEP   0.04f
#define UI_METABALL_EDGE_MARGIN      56
#define UI_FIRST_FRAME_TIMEOUT_MS   500U
#define UI_BACKGROUND_RGB565        0x0000U
#define UI_INITIAL_ENERGY_DB        -120.0f
#define UI_PI                       3.14159265358979323846f

typedef enum {
    UI_PAGE_RADAR = 0,
    UI_PAGE_MIRROR,
    UI_PAGE_WATERFALL,
    UI_PAGE_METABALLS,
    UI_PAGE_LEVEL,
    UI_PAGE_DUAL,
    UI_PAGE_COUNT,
} ui_page_t;

static SemaphoreHandle_t s_lvgl_mutex;
static SemaphoreHandle_t s_state_mutex;
static lv_color_t *s_lvgl_buf1;
static lv_color_t *s_lvgl_buf2;
static esp_timer_handle_t s_lvgl_tick_timer;
static lv_obj_t *s_visualizer;
static audio_spatial_spectrum_ui_state_t s_state;
static float s_doa_trail[UI_DOA_TRAIL_LENGTH];
static uint8_t s_waterfall[UI_WATERFALL_COLUMNS][UI_WATERFALL_ROWS];
static size_t s_waterfall_write_index;
static size_t s_waterfall_count;
static uint8_t s_waterfall_frame_counter;
static float s_level_slow_dbfs = UI_LEVEL_MIN_DBFS;
static float s_metaball_phase_x[UI_METABALL_COUNT] = {0.2f, 2.3f, 4.4f};
static float s_metaball_phase_y[UI_METABALL_COUNT] = {1.1f, 4.0f, 5.3f};
static float s_metaball_level[UI_METABALL_COUNT];
static float s_metaball_doa_offset;
static uint8_t s_doa_trail_count;
static ui_page_t s_page = UI_PAGE_RADAR;
static bool s_ui_ready;

static lv_color_t color_background(void)
{
    return lv_color_make(0, 0, 0);
}

static lv_color_t color_muted(void)
{
    return lv_color_make(70, 91, 98);
}

static lv_color_t color_text(void)
{
    return lv_color_make(222, 232, 230);
}

static uint8_t interpolate_channel(uint8_t start, uint8_t end, uint8_t step)
{
    const int16_t delta = (int16_t)end - (int16_t)start;
    return (uint8_t)((int16_t)start + (delta * step) / 7);
}

static lv_color_t spectrum_color(size_t band)
{
    if (8U > band) {
        const uint8_t step = (uint8_t)band;
        return lv_color_make(interpolate_channel(40U, 72U, step),
                             interpolate_channel(178U, 221U, step),
                             interpolate_channel(255U, 168U, step));
    }
    if (16U > band) {
        const uint8_t step = (uint8_t)(band - 8U);
        return lv_color_make(interpolate_channel(72U, 248U, step),
                             interpolate_channel(221U, 216U, step),
                             interpolate_channel(168U, 77U, step));
    }

    const uint8_t step = (uint8_t)(band - 16U);
    return lv_color_make(interpolate_channel(248U, 255U, step),
                         interpolate_channel(216U, 91U, step),
                         interpolate_channel(77U, 116U, step));
}

static uint8_t interpolate_range(uint8_t start,
                                 uint8_t end,
                                 uint8_t position,
                                 uint8_t range)
{
    if (0U == range) {
        return start;
    }
    const int16_t delta = (int16_t)end - (int16_t)start;
    return (uint8_t)((int16_t)start + (delta * position) / range);
}

static lv_color_t waterfall_color(uint8_t level)
{
    const uint8_t segment = level >> 6;
    const uint8_t position = level & 0x3fU;
    switch (segment) {
    case 0U:
        return lv_color_make(3U,
                             interpolate_range(8U, 46U, position, 63U),
                             interpolate_range(18U, 112U, position, 63U));
    case 1U:
        return lv_color_make(interpolate_range(3U, 20U, position, 63U),
                             interpolate_range(46U, 190U, position, 63U),
                             interpolate_range(112U, 230U, position, 63U));
    case 2U:
        return lv_color_make(interpolate_range(20U, 250U, position, 63U),
                             interpolate_range(190U, 220U, position, 63U),
                             interpolate_range(230U, 70U, position, 63U));
    case 3U:
    default:
        return lv_color_make(255U,
                             interpolate_range(220U, 72U, position, 63U),
                             interpolate_range(70U, 92U, position, 63U));
    }
}

static lv_color_t direction_color(spatial_doa_direction_t direction)
{
    switch (direction) {
    case SPATIAL_DOA_DIRECTION_LEFT:
        return lv_color_make(54, 190, 255);
    case SPATIAL_DOA_DIRECTION_RIGHT:
        return lv_color_make(255, 174, 68);
    case SPATIAL_DOA_DIRECTION_CENTER:
        return lv_color_make(78, 220, 151);
    case SPATIAL_DOA_DIRECTION_IDLE:
    default:
        return color_muted();
    }
}

static void draw_rect(lv_draw_ctx_t *draw_ctx,
                      lv_coord_t x1,
                      lv_coord_t y1,
                      lv_coord_t x2,
                      lv_coord_t y2,
                      lv_color_t color,
                      lv_coord_t radius,
                      lv_opa_t opacity)
{
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = color;
    descriptor.bg_opa = opacity;
    descriptor.radius = radius;
    const lv_area_t area = {
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
    };
    lv_draw_rect(draw_ctx, &descriptor, &area);
}

static void draw_line(lv_draw_ctx_t *draw_ctx,
                      lv_coord_t x1,
                      lv_coord_t y1,
                      lv_coord_t x2,
                      lv_coord_t y2,
                      lv_color_t color,
                      lv_coord_t width,
                      lv_opa_t opacity)
{
    lv_draw_line_dsc_t descriptor;
    lv_draw_line_dsc_init(&descriptor);
    descriptor.color = color;
    descriptor.width = width;
    descriptor.opa = opacity;
    descriptor.round_start = 1U;
    descriptor.round_end = 1U;
    const lv_point_t start = {.x = x1, .y = y1};
    const lv_point_t end = {.x = x2, .y = y2};
    lv_draw_line(draw_ctx, &descriptor, &start, &end);
}

static void draw_text(lv_draw_ctx_t *draw_ctx,
                      lv_coord_t x1,
                      lv_coord_t y1,
                      lv_coord_t x2,
                      lv_coord_t y2,
                      const char *text,
                      const lv_font_t *font,
                      lv_color_t color,
                      lv_text_align_t align)
{
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.font = font;
    descriptor.color = color;
    descriptor.align = align;
    const lv_area_t area = {
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
    };
    lv_draw_label(draw_ctx, &descriptor, &area, text, NULL);
}

static void draw_dot(lv_draw_ctx_t *draw_ctx,
                     lv_coord_t center_x,
                     lv_coord_t center_y,
                     lv_coord_t radius,
                     lv_color_t color,
                     lv_opa_t opacity)
{
    draw_rect(draw_ctx,
              center_x - radius,
              center_y - radius,
              center_x + radius,
              center_y + radius,
              color,
              LV_RADIUS_CIRCLE,
              opacity);
}

static void doa_point(lv_coord_t center_x,
                      lv_coord_t center_y,
                      float angle_deg,
                      float radius,
                      lv_coord_t *point_x,
                      lv_coord_t *point_y)
{
    const float radians = angle_deg * UI_PI / 180.0f;
    *point_x = (lv_coord_t)lroundf((float)center_x + sinf(radians) * radius);
    *point_y = (lv_coord_t)lroundf((float)center_y - cosf(radians) * radius);
}

static void draw_radar(lv_draw_ctx_t *draw_ctx,
                       const lv_area_t *coords,
                       const audio_spatial_spectrum_ui_state_t *state,
                       const float *trail,
                       uint8_t trail_count)
{
    const lv_coord_t center_x = coords->x1 + 160;
    const lv_coord_t center_y = coords->y1 + 120;
    const float inner_radius = 40.0f;
    const float level_range = 34.0f;
    const float doa_radius = 100.0f;

    for (size_t index = 0; index < AUDIO_SPECTRUM_BAND_COUNT; index++) {
        const float radians = ((float)index * 360.0f /
                               (float)AUDIO_SPECTRUM_BAND_COUNT - 90.0f) *
                              UI_PI / 180.0f;
        const float level_radius = inner_radius + 6.0f +
                                   state->combined_levels[index] * level_range;
        const float peak_radius = inner_radius + 6.0f +
                                  state->combined_peaks[index] * level_range;
        const lv_coord_t base_x = (lv_coord_t)lroundf((float)center_x +
                                                      cosf(radians) * inner_radius);
        const lv_coord_t base_y = (lv_coord_t)lroundf((float)center_y +
                                                      sinf(radians) * inner_radius);
        const lv_coord_t limit_x = (lv_coord_t)lroundf((float)center_x +
                                                       cosf(radians) *
                                                       (inner_radius + 6.0f + level_range));
        const lv_coord_t limit_y = (lv_coord_t)lroundf((float)center_y +
                                                       sinf(radians) *
                                                       (inner_radius + 6.0f + level_range));
        const lv_coord_t value_x = (lv_coord_t)lroundf((float)center_x +
                                                       cosf(radians) * level_radius);
        const lv_coord_t value_y = (lv_coord_t)lroundf((float)center_y +
                                                       sinf(radians) * level_radius);
        const lv_coord_t peak_x = (lv_coord_t)lroundf((float)center_x +
                                                      cosf(radians) * peak_radius);
        const lv_coord_t peak_y = (lv_coord_t)lroundf((float)center_y +
                                                      sinf(radians) * peak_radius);

        draw_line(draw_ctx, base_x, base_y, limit_x, limit_y,
                  lv_color_make(27, 46, 51), 5, LV_OPA_COVER);
        draw_line(draw_ctx, base_x, base_y, value_x, value_y,
                  spectrum_color(index), 6, LV_OPA_COVER);
        draw_dot(draw_ctx, peak_x, peak_y, 2, spectrum_color(index), LV_OPA_80);
    }

    lv_coord_t previous_x = 0;
    lv_coord_t previous_y = 0;
    for (int angle = -90; angle <= 90; angle += 10) {
        lv_coord_t point_x = 0;
        lv_coord_t point_y = 0;
        doa_point(center_x, center_y, (float)angle, doa_radius,
                  &point_x, &point_y);
        if (-90 < angle) {
            draw_line(draw_ctx, previous_x, previous_y, point_x, point_y,
                      lv_color_make(48, 67, 72), 2, LV_OPA_COVER);
        }
        previous_x = point_x;
        previous_y = point_y;
    }

    for (uint8_t index = 0U; index < trail_count; index++) {
        lv_coord_t point_x = 0;
        lv_coord_t point_y = 0;
        const uint8_t opacity = (uint8_t)(55U + index * 25U);
        doa_point(center_x, center_y, trail[index], doa_radius,
                  &point_x, &point_y);
        draw_dot(draw_ctx, point_x, point_y, 2,
                 direction_color(state->direction), opacity);
    }

    if (state->doa_active) {
        lv_coord_t cursor_x = 0;
        lv_coord_t cursor_y = 0;
        doa_point(center_x, center_y, state->relative_angle_deg, doa_radius,
                  &cursor_x, &cursor_y);
        draw_dot(draw_ctx, cursor_x, cursor_y, 5,
                 direction_color(state->direction), LV_OPA_COVER);
        draw_dot(draw_ctx, cursor_x, cursor_y, 2, color_text(), LV_OPA_COVER);
    }

    char energy_text[24];
    char angle_text[24];
    snprintf(energy_text, sizeof(energy_text), "%.0f", state->energy_db);
    if (state->doa_active) {
        snprintf(angle_text, sizeof(angle_text), "%+.0f",
                 state->relative_angle_deg);
    } else {
        snprintf(angle_text, sizeof(angle_text), "--");
    }
    draw_text(draw_ctx, center_x - 40, center_y - 13,
              center_x + 40, center_y + 10, energy_text,
              &lv_font_montserrat_20, color_text(), LV_TEXT_ALIGN_CENTER);
    draw_text(draw_ctx, center_x - 42, center_y + 11,
              center_x + 42, center_y + 29, angle_text,
              &lv_font_montserrat_14,
              state->doa_active ? direction_color(state->direction) : color_muted(),
              LV_TEXT_ALIGN_CENTER);

    const lv_coord_t meter_top = coords->y1 + 42;
    const lv_coord_t meter_bottom = coords->y1 + 211;
    const lv_coord_t meter_height = meter_bottom - meter_top;
    const lv_coord_t mic1_fill = (lv_coord_t)lroundf(state->mic1_level * meter_height);
    const lv_coord_t mic2_fill = (lv_coord_t)lroundf(state->mic2_level * meter_height);
    draw_rect(draw_ctx, coords->x1 + 18, meter_top, coords->x1 + 23,
              meter_bottom, lv_color_make(29, 47, 53), 2, LV_OPA_COVER);
    draw_rect(draw_ctx, coords->x2 - 23, meter_top, coords->x2 - 18,
              meter_bottom, lv_color_make(29, 47, 53), 2, LV_OPA_COVER);
    draw_rect(draw_ctx, coords->x1 + 18, meter_bottom - mic1_fill,
              coords->x1 + 23, meter_bottom, lv_color_make(48, 190, 255),
              2, LV_OPA_COVER);
    draw_rect(draw_ctx, coords->x2 - 23, meter_bottom - mic2_fill,
              coords->x2 - 18, meter_bottom, lv_color_make(255, 165, 71),
              2, LV_OPA_COVER);
}

static void draw_frequency_bars(lv_draw_ctx_t *draw_ctx,
                                const lv_area_t *coords,
                                lv_coord_t baseline,
                                lv_coord_t maximum_height,
                                const float *levels,
                                const float *peaks)
{
    const lv_coord_t start_x = coords->x1 + 17;
    const lv_coord_t bar_width = 10;
    const lv_coord_t gap = 2;

    for (size_t index = 0; index < AUDIO_SPECTRUM_BAND_COUNT; index++) {
        const lv_coord_t x1 = start_x + (lv_coord_t)index * (bar_width + gap);
        const lv_coord_t level_height =
            (lv_coord_t)lroundf(levels[index] * maximum_height);
        const lv_coord_t peak_y = baseline -
            (lv_coord_t)lroundf(peaks[index] * maximum_height);
        draw_rect(draw_ctx, x1, baseline - maximum_height,
                  x1 + bar_width - 1, baseline,
                  lv_color_make(24, 41, 47), 2, LV_OPA_COVER);
        if (0 < level_height) {
            draw_rect(draw_ctx, x1, baseline - level_height,
                      x1 + bar_width - 1, baseline,
                      spectrum_color(index), 2, LV_OPA_COVER);
        }
        draw_line(draw_ctx, x1 + 1, peak_y, x1 + bar_width - 2, peak_y,
                  color_text(), 1, LV_OPA_70);
    }
}

static void draw_dual(lv_draw_ctx_t *draw_ctx,
                      const lv_area_t *coords,
                      const audio_spatial_spectrum_ui_state_t *state)
{
    draw_line(draw_ctx, coords->x1 + 17, coords->y1 + 8,
              coords->x2 - 17, coords->y1 + 8,
              lv_color_make(70, 196, 255), 3, LV_OPA_COVER);
    draw_frequency_bars(draw_ctx, coords, coords->y1 + 111, 92,
                        state->mic1_levels, state->mic1_peaks);

    draw_line(draw_ctx, coords->x1 + 17, coords->y1 + 120,
              coords->x2 - 17, coords->y1 + 120,
              lv_color_make(29, 47, 53), 1, LV_OPA_COVER);

    draw_line(draw_ctx, coords->x1 + 17, coords->y1 + 129,
              coords->x2 - 17, coords->y1 + 129,
              lv_color_make(255, 174, 72), 3, LV_OPA_COVER);
    draw_frequency_bars(draw_ctx, coords, coords->y1 + 232, 92,
                        state->mic2_levels, state->mic2_peaks);
}

static void draw_mirror(lv_draw_ctx_t *draw_ctx,
                        const lv_area_t *coords,
                        const audio_spatial_spectrum_ui_state_t *state)
{
    const lv_coord_t center_y = coords->y1 + 120;
    const lv_coord_t start_x = coords->x1 + 5;
    const lv_coord_t bar_width = 9;
    const lv_coord_t gap = 4;
    const lv_coord_t maximum_height = 102;

    draw_line(draw_ctx, coords->x1 + 5, center_y,
              coords->x2 - 5, center_y,
              lv_color_make(45, 67, 72), 1, LV_OPA_COVER);
    for (size_t index = 0U; index < AUDIO_SPECTRUM_BAND_COUNT; index++) {
        const lv_coord_t x1 = start_x + (lv_coord_t)index * (bar_width + gap);
        const lv_coord_t level_height =
            (lv_coord_t)lroundf(state->combined_levels[index] * maximum_height);
        const lv_coord_t peak_offset =
            (lv_coord_t)lroundf(state->combined_peaks[index] * maximum_height);
        const lv_color_t color = spectrum_color(index);

        draw_rect(draw_ctx, x1, center_y - maximum_height,
                  x1 + bar_width - 1, center_y + maximum_height,
                  lv_color_make(18, 33, 39), 2, LV_OPA_COVER);
        if (0 < level_height) {
            draw_rect(draw_ctx, x1, center_y - level_height,
                      x1 + bar_width - 1, center_y - 1,
                      color, 2, LV_OPA_COVER);
            draw_rect(draw_ctx, x1, center_y + 1,
                      x1 + bar_width - 1, center_y + level_height,
                      color, 2, LV_OPA_70);
        }
        draw_line(draw_ctx, x1 + 1, center_y - peak_offset,
                  x1 + bar_width - 2, center_y - peak_offset,
                  color_text(), 1, LV_OPA_70);
        draw_line(draw_ctx, x1 + 1, center_y + peak_offset,
                  x1 + bar_width - 2, center_y + peak_offset,
                  color_text(), 1, LV_OPA_50);
    }

    if (state->doa_active) {
        const float normalized = state->relative_angle_deg / 90.0f;
        const lv_coord_t cursor_x = coords->x1 + 160 +
            (lv_coord_t)lroundf(normalized * 148.0f);
        draw_line(draw_ctx, cursor_x, coords->y1 + 8,
                  cursor_x, coords->y2 - 8,
                  direction_color(state->direction), 2, LV_OPA_80);
        draw_dot(draw_ctx, cursor_x, center_y, 4,
                 direction_color(state->direction), LV_OPA_COVER);
    }
}

static void draw_waterfall(
    lv_draw_ctx_t *draw_ctx,
    const lv_area_t *coords,
    const uint8_t history[UI_WATERFALL_COLUMNS][UI_WATERFALL_ROWS],
    size_t write_index,
    size_t history_count)
{
    const lv_coord_t left = coords->x1 + 20;
    const lv_coord_t bottom = coords->y2 - 8;
    const lv_coord_t cell_width = 7;
    const lv_coord_t cell_height = 18;
    const size_t visible_count = history_count < UI_WATERFALL_COLUMNS ?
                                 history_count : UI_WATERFALL_COLUMNS;
    const size_t oldest_index = UI_WATERFALL_COLUMNS == visible_count ?
                                write_index : 0U;
    const size_t leading_columns = UI_WATERFALL_COLUMNS - visible_count;

    for (size_t column = 0U; column < visible_count; column++) {
        const size_t source_column =
            (oldest_index + column) % UI_WATERFALL_COLUMNS;
        const lv_coord_t x1 = left +
            (lv_coord_t)(leading_columns + column) * cell_width;
        for (size_t row = 0U; row < UI_WATERFALL_ROWS; row++) {
            const lv_coord_t y2 = bottom - (lv_coord_t)row * cell_height;
            draw_rect(draw_ctx, x1, y2 - cell_height + 2,
                      x1 + cell_width - 2, y2,
                      waterfall_color(history[source_column][row]),
                      1, LV_OPA_COVER);
        }
    }

    for (size_t row = 3U; row < UI_WATERFALL_ROWS; row += 3U) {
        const lv_coord_t y = bottom - (lv_coord_t)row * cell_height;
        draw_line(draw_ctx, left, y,
                  left + (lv_coord_t)UI_WATERFALL_COLUMNS * cell_width, y,
                  lv_color_make(92, 108, 110), 1, LV_OPA_30);
    }
}

static void draw_energy_ball(lv_draw_ctx_t *draw_ctx,
                             lv_coord_t center_x,
                             lv_coord_t center_y,
                             lv_coord_t radius,
                             lv_color_t color)
{
    draw_dot(draw_ctx, center_x, center_y, radius + 10, color, 24U);
    draw_dot(draw_ctx, center_x, center_y, radius + 5, color, 48U);
    draw_dot(draw_ctx, center_x, center_y, radius, color, 150U);
    draw_dot(draw_ctx, center_x, center_y,
             radius > 8 ? radius - 8 : radius,
             lv_color_mix(color_text(), color, 64U), 190U);
}

static float smooth_with_limit(float current,
                               float target,
                               float alpha,
                               float maximum_step)
{
    float step = (target - current) * alpha;
    if (maximum_step < step) {
        step = maximum_step;
    } else if (-maximum_step > step) {
        step = -maximum_step;
    }
    return current + step;
}

static lv_coord_t clamp_metaball_position(float position,
                                          lv_coord_t minimum,
                                          lv_coord_t maximum)
{
    if ((float)minimum > position) {
        return minimum;
    }
    if ((float)maximum < position) {
        return maximum;
    }
    return (lv_coord_t)lroundf(position);
}

static void draw_metaballs(lv_draw_ctx_t *draw_ctx,
                           const lv_area_t *coords,
                           const audio_spatial_spectrum_ui_state_t *state)
{
    static const lv_coord_t s_base_x[UI_METABALL_COUNT] = {100, 160, 220};
    static const lv_coord_t s_base_y[UI_METABALL_COUNT] = {130, 112, 130};
    static const float s_amplitude_x[UI_METABALL_COUNT] = {48.0f, 64.0f, 50.0f};
    static const float s_amplitude_y[UI_METABALL_COUNT] = {55.0f, 58.0f, 50.0f};
    static const lv_coord_t s_base_radius[UI_METABALL_COUNT] = {14, 12, 10};
    static const float s_radius_range[UI_METABALL_COUNT] = {38.0f, 32.0f, 26.0f};
    const float target_level[UI_METABALL_COUNT] = {
        audio_spectrum_average_band_levels(
            state->combined_levels, AUDIO_SPECTRUM_BAND_COUNT, 0U, 8U),
        audio_spectrum_average_band_levels(
            state->combined_levels, AUDIO_SPECTRUM_BAND_COUNT, 8U, 8U),
        audio_spectrum_average_band_levels(
            state->combined_levels, AUDIO_SPECTRUM_BAND_COUNT, 16U, 8U),
    };
    const lv_color_t color[UI_METABALL_COUNT] = {
        lv_color_make(34, 187, 255),
        lv_color_make(61, 221, 151),
        lv_color_make(255, 183, 65),
    };
    const float target_doa_offset = state->doa_active ?
        state->relative_angle_deg * (UI_METABALL_DOA_RANGE_PX / 90.0f) : 0.0f;
    s_metaball_doa_offset = smooth_with_limit(
        s_metaball_doa_offset, target_doa_offset,
        UI_METABALL_DOA_ALPHA, UI_METABALL_DOA_MAX_STEP_PX);

    for (size_t index = 0U; index < UI_METABALL_COUNT; index++) {
        const float level_alpha = target_level[index] > s_metaball_level[index] ?
                                  UI_METABALL_LEVEL_ATTACK :
                                  UI_METABALL_LEVEL_RELEASE;
        s_metaball_level[index] = smooth_with_limit(
            s_metaball_level[index], target_level[index], level_alpha,
            UI_METABALL_LEVEL_MAX_STEP);

        const float center_x = (float)(coords->x1 + s_base_x[index]) +
            sinf(s_metaball_phase_x[index]) * s_amplitude_x[index] +
            s_metaball_doa_offset;
        const float center_y = (float)(coords->y1 + s_base_y[index]) +
            cosf(s_metaball_phase_y[index]) * s_amplitude_y[index];
        const lv_coord_t x = clamp_metaball_position(
            center_x, coords->x1 + UI_METABALL_EDGE_MARGIN,
            coords->x2 - UI_METABALL_EDGE_MARGIN);
        const lv_coord_t y = clamp_metaball_position(
            center_y, coords->y1 + UI_METABALL_EDGE_MARGIN,
            coords->y2 - UI_METABALL_EDGE_MARGIN);
        const lv_coord_t radius = s_base_radius[index] +
            (lv_coord_t)lroundf(s_metaball_level[index] * s_radius_range[index]);
        draw_energy_ball(draw_ctx, x, y, radius, color[index]);
    }
}

static void draw_level_meter(lv_draw_ctx_t *draw_ctx,
                             const lv_area_t *coords,
                             lv_coord_t y,
                             lv_coord_t height,
                             float level,
                             lv_color_t color)
{
    const lv_coord_t x1 = coords->x1 + 24;
    const lv_coord_t x2 = coords->x2 - 24;
    const lv_coord_t width = x2 - x1;
    const lv_coord_t fill = (lv_coord_t)lroundf(level * (float)width);
    draw_rect(draw_ctx, x1, y, x2, y + height,
              lv_color_make(22, 40, 46), height / 2, LV_OPA_COVER);
    if (0 < fill) {
        draw_rect(draw_ctx, x1, y, x1 + fill, y + height,
                  color, height / 2, LV_OPA_COVER);
    }
    for (size_t tick = 1U; tick < 6U; tick++) {
        const lv_coord_t tick_x = x1 + (lv_coord_t)tick * width / 6;
        draw_line(draw_ctx, tick_x, y - 3, tick_x, y + height + 3,
                  color_muted(), 1, LV_OPA_50);
    }
}

static void draw_level(lv_draw_ctx_t *draw_ctx,
                       const lv_area_t *coords,
                       const audio_spatial_spectrum_ui_state_t *state,
                       float slow_dbfs)
{
    const float fast_level = audio_spectrum_level_from_db(
        state->energy_dbfs, UI_LEVEL_VISIBLE_FLOOR_DBFS, 0.0f);
    const float slow_level = audio_spectrum_level_from_db(
        slow_dbfs, UI_LEVEL_VISIBLE_FLOOR_DBFS, 0.0f);
    char value_text[16] = {0};
    snprintf(value_text, sizeof(value_text), "%.0f", state->energy_dbfs);

    draw_text(draw_ctx, coords->x1 + 80, coords->y1 + 36,
              coords->x2 - 80, coords->y1 + 82, value_text,
              &lv_font_montserrat_32, color_text(), LV_TEXT_ALIGN_CENTER);
    draw_level_meter(draw_ctx, coords, coords->y1 + 126, 22,
                     fast_level, lv_color_make(46, 198, 255));
    draw_level_meter(draw_ctx, coords, coords->y1 + 176, 10,
                     slow_level, lv_color_make(255, 184, 66));

    const lv_coord_t center_x = coords->x1 + 160;
    const lv_coord_t mic1_x = center_x - 52;
    const lv_coord_t mic2_x = center_x + 52;
    draw_dot(draw_ctx, mic1_x, coords->y1 + 220,
             4 + (lv_coord_t)lroundf(state->mic1_level * 8.0f),
             lv_color_make(48, 190, 255), LV_OPA_80);
    draw_dot(draw_ctx, mic2_x, coords->y1 + 220,
             4 + (lv_coord_t)lroundf(state->mic2_level * 8.0f),
             lv_color_make(255, 165, 71), LV_OPA_80);
}

static void visualizer_draw_cb(lv_event_t *event)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *object = lv_event_get_target(event);
    lv_area_t coords;
    audio_spatial_spectrum_ui_state_t state;
    float trail[UI_DOA_TRAIL_LENGTH] = {0};
    uint8_t waterfall[UI_WATERFALL_COLUMNS][UI_WATERFALL_ROWS] = {{0}};
    size_t waterfall_write_index = 0U;
    size_t waterfall_count = 0U;
    float slow_dbfs = UI_LEVEL_MIN_DBFS;
    uint8_t trail_count = 0U;

    lv_obj_get_coords(object, &coords);
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    state = s_state;
    trail_count = s_doa_trail_count;
    memcpy(trail, s_doa_trail, sizeof(trail));
    memcpy(waterfall, s_waterfall, sizeof(waterfall));
    waterfall_write_index = s_waterfall_write_index;
    waterfall_count = s_waterfall_count;
    slow_dbfs = s_level_slow_dbfs;
    xSemaphoreGive(s_state_mutex);

    draw_rect(draw_ctx, coords.x1, coords.y1, coords.x2, coords.y2,
              color_background(), 0, LV_OPA_COVER);
    switch (s_page) {
    case UI_PAGE_MIRROR:
        draw_mirror(draw_ctx, &coords, &state);
        break;
    case UI_PAGE_WATERFALL:
        draw_waterfall(draw_ctx, &coords, waterfall,
                       waterfall_write_index, waterfall_count);
        break;
    case UI_PAGE_METABALLS:
        draw_metaballs(draw_ctx, &coords, &state);
        break;
    case UI_PAGE_LEVEL:
        draw_level(draw_ctx, &coords, &state, slow_dbfs);
        break;
    case UI_PAGE_DUAL:
        draw_dual(draw_ctx, &coords, &state);
        break;
    case UI_PAGE_RADAR:
    default:
        draw_radar(draw_ctx, &coords, &state, trail, trail_count);
        break;
    }
}

static void visualizer_gesture_cb(lv_event_t *event)
{
    (void)event;
    lv_indev_t *input = lv_indev_get_act();
    if (NULL == input) {
        return;
    }

    const lv_dir_t direction = lv_indev_get_gesture_dir(input);
    int step = 0;
    if (LV_DIR_LEFT == direction) {
        step = 1;
    } else if (LV_DIR_RIGHT == direction) {
        step = -1;
    }
    if (0 == step) {
        return;
    }

    s_page = (ui_page_t)audio_spectrum_mode_step((size_t)s_page,
                                                  step,
                                                  (size_t)UI_PAGE_COUNT);
    lv_obj_invalidate(s_visualizer);
    lv_indev_wait_release(input);
    ESP_LOGI(TAG, "visual mode=%u", (unsigned)s_page);
}

static esp_err_t create_screen(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, color_background(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    s_visualizer = lv_obj_create(screen);
    lv_obj_remove_style_all(s_visualizer);
    lv_obj_set_pos(s_visualizer, 0, UI_VISUALIZER_Y);
    lv_obj_set_size(s_visualizer, UI_HOR_RES, UI_VISUALIZER_HEIGHT);
    lv_obj_clear_flag(s_visualizer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_visualizer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_visualizer, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_visualizer, visualizer_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_visualizer, visualizer_gesture_cb,
                        LV_EVENT_GESTURE, NULL);
    return ESP_OK;
}

static void lvgl_flush_cb(lv_disp_drv_t *driver,
                          const lv_area_t *area,
                          lv_color_t *color_map)
{
    const uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                                 (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;
    for (uint32_t index = 0; index < pixel_count; index++) {
        const uint16_t pixel = pixels[index];
        const uint16_t red = (pixel >> 11) & 0x1fU;
        const uint16_t green = (pixel >> 5) & 0x3fU;
        const uint16_t blue = pixel & 0x1fU;
        const uint16_t bgr = (uint16_t)((blue << 11) | (green << 5) | red);
        pixels[index] = (uint16_t)((bgr >> 8) | (bgr << 8));
    }

    esp_err_t ret = display_hal_draw_bitmap_rgb565(area->x1,
                                                    area->y1,
                                                    area->x2 - area->x1 + 1,
                                                    area->y2 - area->y1 + 1,
                                                    pixels);
    if (ESP_OK == ret) {
        ret = display_hal_wait_pending(1000U);
    }
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(ret));
    }
    lv_disp_flush_ready(driver);
}

static void lvgl_touch_read_cb(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    static lv_coord_t last_x;
    static lv_coord_t last_y;
    touch_panel_point_t point = {0};
    uint8_t touch_count = 0U;
    const esp_err_t ret = touch_panel_read_point(&point, &touch_count);

    if (ESP_OK == ret && 0U < touch_count && 2U >= touch_count) {
        last_x = (lv_coord_t)(BOARD_LAIWFS300_LCD_V_RES - 1U - point.y);
        last_y = (lv_coord_t)point.x;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(UI_TICK_MS);
}

static void render_timer_cb(lv_timer_t *timer)
{
    static const float s_phase_step_x[UI_METABALL_COUNT] = {
        0.025f, 0.033f, 0.021f,
    };
    static const float s_phase_step_y[UI_METABALL_COUNT] = {
        0.018f, 0.027f, 0.037f,
    };
    (void)timer;

    for (size_t index = 0U; index < UI_METABALL_COUNT; index++) {
        s_metaball_phase_x[index] += s_phase_step_x[index];
        s_metaball_phase_y[index] += s_phase_step_y[index];
        if ((2.0f * UI_PI) <= s_metaball_phase_x[index]) {
            s_metaball_phase_x[index] -= 2.0f * UI_PI;
        }
        if ((2.0f * UI_PI) <= s_metaball_phase_y[index]) {
            s_metaball_phase_y[index] -= 2.0f * UI_PI;
        }
    }
    lv_obj_invalidate(s_visualizer);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (true) {
        xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGiveRecursive(s_lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(UI_TASK_DELAY_MS));
    }
}

static bool initialize_touch(void)
{
    for (uint32_t attempt = 0U; attempt < UI_TOUCH_RETRY_COUNT; attempt++) {
        if (ESP_OK == board_laiwfs300_touch_init()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(UI_TOUCH_RETRY_DELAY_MS));
    }
    return false;
}

esp_err_t audio_spatial_spectrum_ui_init(void)
{
    if (s_ui_ready) {
        return ESP_OK;
    }

    esp_err_t ret = board_laiwfs300_display_init_with_config(
        UI_LCD_PIXEL_CLOCK_HZ, UI_BUFFER_ROWS);
    if (ESP_OK != ret) {
        return ret;
    }
    ESP_RETURN_ON_ERROR(display_hal_set_orientation(true, false, true),
                        TAG, "set landscape orientation");
    ESP_RETURN_ON_ERROR(board_laiwfs300_display_fill_rgb565(UI_BACKGROUND_RGB565),
                        TAG, "fill display");

    const bool touch_ready = initialize_touch();
    if (!touch_ready) {
        ESP_LOGW(TAG, "touch init failed, page remains on RADAR");
    }

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    s_state_mutex = xSemaphoreCreateMutex();
    if (NULL == s_lvgl_mutex || NULL == s_state_mutex) {
        return ESP_ERR_NO_MEM;
    }
    s_state.energy_db = UI_INITIAL_ENERGY_DB;
    s_state.energy_dbfs = UI_LEVEL_MIN_DBFS;
    s_level_slow_dbfs = UI_LEVEL_MIN_DBFS;

    lv_init();
    const size_t buffer_pixels = UI_HOR_RES * UI_BUFFER_ROWS;
    s_lvgl_buf1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                   MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_lvgl_buf2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                   MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == s_lvgl_buf1 || NULL == s_lvgl_buf2) {
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buffer;
    lv_disp_draw_buf_init(&draw_buffer, s_lvgl_buf1, s_lvgl_buf2, buffer_pixels);

    static lv_disp_drv_t display_driver;
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = UI_HOR_RES;
    display_driver.ver_res = UI_VER_RES;
    display_driver.flush_cb = lvgl_flush_cb;
    display_driver.draw_buf = &draw_buffer;
    lv_disp_drv_register(&display_driver);

    if (touch_ready) {
        static lv_indev_drv_t input_driver;
        lv_indev_drv_init(&input_driver);
        input_driver.type = LV_INDEV_TYPE_POINTER;
        input_driver.read_cb = lvgl_touch_read_cb;
        lv_indev_drv_register(&input_driver);
    }

    ESP_RETURN_ON_ERROR(create_screen(), TAG, "create screen");
    lv_timer_create(render_timer_cb, UI_RENDER_INTERVAL_MS, NULL);
    lv_timer_handler();
    ESP_RETURN_ON_ERROR(display_hal_wait_pending(UI_FIRST_FRAME_TIMEOUT_MS),
                        TAG, "present first frame");

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "spectrum_tick",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &s_lvgl_tick_timer),
                        TAG, "create tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_lvgl_tick_timer,
                                                  UI_TICK_MS * 1000U),
                        TAG, "start tick timer");

    const BaseType_t created = xTaskCreate(lvgl_task, "spectrum_lvgl",
                                           UI_TASK_STACK, NULL,
                                           UI_TASK_PRIORITY, NULL);
    if (pdPASS != created) {
        return ESP_ERR_NO_MEM;
    }

    s_ui_ready = true;
    ESP_LOGI(TAG, "UI ready: %ux%u, LCD=%u MHz, buffers=2x%u rows, touch=%s",
             UI_HOR_RES, UI_VER_RES, UI_LCD_PIXEL_CLOCK_HZ / 1000000U,
             UI_BUFFER_ROWS, touch_ready ? "ready" : "unavailable");
    return ESP_OK;
}

esp_err_t audio_spatial_spectrum_ui_update(
    const audio_spatial_spectrum_ui_state_t *state)
{
    if (!s_ui_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == state) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_state = *state;
    const float fast_dbfs = isfinite(state->energy_dbfs) ?
                            state->energy_dbfs : UI_LEVEL_MIN_DBFS;
    s_level_slow_dbfs += UI_LEVEL_SLOW_ALPHA *
                         (fast_dbfs - s_level_slow_dbfs);

    s_waterfall_frame_counter++;
    if (UI_WATERFALL_FRAME_DIVIDER <= s_waterfall_frame_counter) {
        s_waterfall_frame_counter = 0U;
        for (size_t row = 0U; row < UI_WATERFALL_ROWS; row++) {
            const float level = audio_spectrum_average_band_levels(
                state->combined_levels, AUDIO_SPECTRUM_BAND_COUNT,
                row * 2U, 2U);
            s_waterfall[s_waterfall_write_index][row] =
                (uint8_t)lroundf(level * 255.0f);
        }
        s_waterfall_write_index =
            (s_waterfall_write_index + 1U) % UI_WATERFALL_COLUMNS;
        if (UI_WATERFALL_COLUMNS > s_waterfall_count) {
            s_waterfall_count++;
        }
    }

    if (state->doa_active) {
        if (UI_DOA_TRAIL_LENGTH > s_doa_trail_count) {
            s_doa_trail[s_doa_trail_count++] = state->relative_angle_deg;
        } else {
            memmove(&s_doa_trail[0], &s_doa_trail[1],
                    (UI_DOA_TRAIL_LENGTH - 1U) * sizeof(s_doa_trail[0]));
            s_doa_trail[UI_DOA_TRAIL_LENGTH - 1U] = state->relative_angle_deg;
        }
    } else {
        s_doa_trail_count = 0U;
    }
    xSemaphoreGive(s_state_mutex);
    return ESP_OK;
}
