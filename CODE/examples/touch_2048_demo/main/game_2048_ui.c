/*
 * The LVGL 8 board and interaction structure is informed by
 * 100askTeam/lv_lib_100ask at commit
 * b1cdbac458041a996948ff130305428a3baa5874 (MIT). The local version uses
 * a separate pure-C model and gesture state machine.
 * See ../THIRD_PARTY_NOTICES.md.
 */

#include "game_2048_ui.h"

#include "game_2048_gesture.h"

#include "lvgl.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define UI_SCREEN_WIDTH 320
#define UI_SCREEN_HEIGHT 240
#define UI_BOARD_X 4
#define UI_BOARD_Y 4
#define UI_BOARD_SIZE 232
#define UI_BOARD_PADDING 4
#define UI_CELL_GAP 4
#define UI_CELL_SIZE 53
#define UI_PANEL_X 240
#define UI_PANEL_WIDTH 80
#define UI_PANEL_CONTENT_X 242
#define UI_PANEL_CONTENT_WIDTH 76
#define UI_BUTTON_Y 198
#define UI_BUTTON_SIZE 34
#define UI_BUTTON_GAP 6
#define UI_OVERLAY_X 10
#define UI_OVERLAY_Y 88
#define UI_OVERLAY_WIDTH 212
#define UI_OVERLAY_HEIGHT 56
#define UI_WIN_NOTICE_MS 1400U
#define UI_TILE_TEXT_LENGTH 16
#define UI_SCORE_TEXT_LENGTH 11
#define UI_DECIMAL_TILE_MAX_EXPONENT 19U

_Static_assert((2 * UI_BOARD_PADDING) +
                       (GAME_2048_BOARD_SIDE * UI_CELL_SIZE) +
                       ((GAME_2048_BOARD_SIDE - 1) * UI_CELL_GAP) ==
                   UI_BOARD_SIZE,
               "board geometry must be exact");
_Static_assert(UI_BOARD_X + UI_BOARD_SIZE <= UI_PANEL_X,
               "board and side panel must not overlap");
_Static_assert(UI_PANEL_X + UI_PANEL_WIDTH == UI_SCREEN_WIDTH,
               "side panel must end at the screen edge");
_Static_assert(UI_BUTTON_Y + UI_BUTTON_SIZE <= UI_SCREEN_HEIGHT,
               "buttons must fit vertically");
_Static_assert(UI_PANEL_CONTENT_X + (2 * UI_BUTTON_SIZE) + UI_BUTTON_GAP <=
                   UI_SCREEN_WIDTH,
               "buttons must fit horizontally");

typedef enum {
    UI_OVERLAY_NONE = 0,
    UI_OVERLAY_WIN,
    UI_OVERLAY_LOST,
} ui_overlay_state_t;

static game_2048_ui_callbacks_t s_callbacks;
static game_2048_gesture_t s_gesture;
static lv_obj_t *s_screen;
static lv_obj_t *s_board;
static lv_obj_t *s_cells[GAME_2048_CELL_COUNT];
static lv_obj_t *s_cell_labels[GAME_2048_CELL_COUNT];
static lv_obj_t *s_gesture_layer;
static lv_obj_t *s_score_value;
static lv_obj_t *s_best_value;
static lv_obj_t *s_status_label;
static lv_obj_t *s_undo_button;
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_label;
static lv_timer_t *s_win_timer;
static ui_overlay_state_t s_overlay_state;
static bool s_initialized;

static lv_color_t color_hex(uint32_t value)
{
    return lv_color_hex(value);
}

static lv_color_t tile_background(uint8_t exponent)
{
    static const uint32_t colors[] = {
        0xD7DDD9, 0xE7F5EC, 0xF6D365, 0xF39A8D,
        0x78C6E7, 0xA995D6, 0xED8452, 0x59BDB8,
        0xE46751, 0x398C78, 0xD9AD43, 0xC94C64,
    };
    const size_t color_count = sizeof(colors) / sizeof(colors[0]);

    if (color_count > exponent) {
        return color_hex(colors[exponent]);
    }
    return (0U == (exponent % 2U)) ? color_hex(0x315B67)
                                    : color_hex(0x6B4F75);
}

static lv_color_t tile_foreground(uint8_t exponent)
{
    return (7U <= exponent) ? color_hex(0xFFFFFF) : color_hex(0x253238);
}

static const lv_font_t *tile_font(uint8_t exponent)
{
    if (7U >= exponent) {
        return &lv_font_montserrat_20;
    }
    if (13U >= exponent) {
        return &lv_font_montserrat_16;
    }
    return &lv_font_montserrat_12;
}

static void format_tile(uint8_t exponent, char *text, size_t text_size)
{
    if ((NULL == text) || (0U == text_size)) {
        return;
    }
    if (0U == exponent) {
        text[0] = '\0';
        return;
    }
    if (UI_DECIMAL_TILE_MAX_EXPONENT >= exponent) {
        const uint32_t value = UINT32_C(1) << exponent;
        (void)snprintf(text, text_size, "%" PRIu32, value);
        return;
    }
    (void)snprintf(text, text_size, "2^%u", (unsigned int)exponent);
}

static void set_score_text(lv_obj_t *label, uint32_t score)
{
    char text[UI_SCORE_TEXT_LENGTH] = {0};
    (void)snprintf(text, sizeof(text), "%" PRIu32, score);
    lv_obj_set_style_text_font(label,
                               (7U < strlen(text)) ? &lv_font_montserrat_12
                                                   : &lv_font_montserrat_16,
                               0);
    lv_label_set_text(label, text);
}

static lv_obj_t *create_label(lv_obj_t *parent,
                              int32_t x,
                              int32_t y,
                              int32_t width,
                              int32_t height,
                              const lv_font_t *font,
                              lv_color_t color,
                              const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    if (NULL == label) {
        return NULL;
    }
    lv_obj_remove_style_all(label);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
    return label;
}

static void cancel_win_timer(void)
{
    if (NULL != s_win_timer) {
        lv_timer_del(s_win_timer);
        s_win_timer = NULL;
    }
}

static void hide_overlay(void)
{
    if (NULL != s_overlay) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    s_overlay_state = UI_OVERLAY_NONE;
}

static void win_timer_cb(lv_timer_t *timer)
{
    if (s_win_timer == timer) {
        s_win_timer = NULL;
    }
    if (UI_OVERLAY_WIN == s_overlay_state) {
        hide_overlay();
    }
    lv_timer_del(timer);
}

static void show_overlay(const char *text, ui_overlay_state_t state)
{
    if ((NULL == text) || (NULL == s_overlay) || (NULL == s_overlay_label)) {
        return;
    }
    lv_label_set_text(s_overlay_label, text);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_overlay_state = state;
}

static bool get_active_point(lv_point_t *point)
{
    if (NULL == point) {
        return false;
    }
    lv_indev_t *input = lv_indev_get_act();
    if (NULL == input) {
        return false;
    }
    lv_indev_get_point(input, point);
    return true;
}

static game_2048_direction_t gesture_to_direction(game_2048_gesture_event_t event)
{
    switch (event) {
    case GAME_2048_GESTURE_UP:
        return GAME_2048_DIRECTION_UP;
    case GAME_2048_GESTURE_DOWN:
        return GAME_2048_DIRECTION_DOWN;
    case GAME_2048_GESTURE_LEFT:
        return GAME_2048_DIRECTION_LEFT;
    case GAME_2048_GESTURE_RIGHT:
        return GAME_2048_DIRECTION_RIGHT;
    case GAME_2048_GESTURE_NONE:
    default:
        return GAME_2048_DIRECTION_UP;
    }
}

static void process_gesture_point(const lv_point_t *point)
{
    if ((NULL == point) || !s_gesture.active || s_gesture.locked) {
        return;
    }
    const game_2048_gesture_event_t gesture_event =
        game_2048_gesture_update(&s_gesture, point->x, point->y);
    if ((GAME_2048_GESTURE_NONE != gesture_event) &&
        (NULL != s_callbacks.on_direction)) {
        s_callbacks.on_direction(s_callbacks.ctx,
                                 gesture_to_direction(gesture_event));
    }
}

static void gesture_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point = {0};

    if (LV_EVENT_PRESSED == code) {
        if (!get_active_point(&point) ||
            !game_2048_gesture_begin(&s_gesture, point.x, point.y)) {
            if (NULL != s_callbacks.on_touch_rejected) {
                s_callbacks.on_touch_rejected(s_callbacks.ctx);
            }
        }
        return;
    }
    if (LV_EVENT_PRESSING == code) {
        if (get_active_point(&point)) {
            process_gesture_point(&point);
        }
        return;
    }
    if (LV_EVENT_RELEASED == code) {
        if (get_active_point(&point)) {
            process_gesture_point(&point);
        }
        if (s_gesture.active && !s_gesture.locked &&
            (NULL != s_callbacks.on_touch_rejected)) {
            s_callbacks.on_touch_rejected(s_callbacks.ctx);
        }
        game_2048_gesture_end(&s_gesture);
        return;
    }
    if (LV_EVENT_PRESS_LOST == code) {
        if (s_gesture.active && !s_gesture.locked &&
            (NULL != s_callbacks.on_touch_rejected)) {
            s_callbacks.on_touch_rejected(s_callbacks.ctx);
        }
        game_2048_gesture_end(&s_gesture);
    }
}

static void undo_event_cb(lv_event_t *event)
{
    if ((LV_EVENT_CLICKED == lv_event_get_code(event)) &&
        (NULL != s_callbacks.on_undo)) {
        s_callbacks.on_undo(s_callbacks.ctx);
    }
}

static void restart_event_cb(lv_event_t *event)
{
    if ((LV_EVENT_CLICKED == lv_event_get_code(event)) &&
        (NULL != s_callbacks.on_restart)) {
        s_callbacks.on_restart(s_callbacks.ctx);
    }
}

static lv_obj_t *create_icon_button(lv_obj_t *parent,
                                    int32_t x,
                                    const char *symbol,
                                    lv_event_cb_t event_cb)
{
    lv_obj_t *button = lv_btn_create(parent);
    if (NULL == button) {
        return NULL;
    }
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, UI_BUTTON_Y);
    lv_obj_set_size(button, UI_BUTTON_SIZE, UI_BUTTON_SIZE);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x2F6F65), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x24584F), LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    if (NULL == label) {
        lv_obj_del(button);
        return NULL;
    }
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, color_hex(0xFFFFFF), 0);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    return button;
}

static bool create_board(void)
{
    s_board = lv_obj_create(s_screen);
    if (NULL == s_board) {
        return false;
    }
    lv_obj_remove_style_all(s_board);
    lv_obj_set_pos(s_board, UI_BOARD_X, UI_BOARD_Y);
    lv_obj_set_size(s_board, UI_BOARD_SIZE, UI_BOARD_SIZE);
    lv_obj_set_style_radius(s_board, 6, 0);
    lv_obj_set_style_bg_color(s_board, color_hex(0x303A3D), 0);
    lv_obj_set_style_bg_opa(s_board, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t row = 0U; row < GAME_2048_BOARD_SIDE; ++row) {
        for (size_t column = 0U; column < GAME_2048_BOARD_SIDE; ++column) {
            const size_t index = (row * GAME_2048_BOARD_SIDE) + column;
            lv_obj_t *cell = lv_obj_create(s_board);
            if (NULL == cell) {
                return false;
            }
            s_cells[index] = cell;
            lv_obj_remove_style_all(cell);
            lv_obj_set_pos(cell,
                           UI_BOARD_PADDING +
                               (int32_t)column * (UI_CELL_SIZE + UI_CELL_GAP),
                           UI_BOARD_PADDING +
                               (int32_t)row * (UI_CELL_SIZE + UI_CELL_GAP));
            lv_obj_set_size(cell, UI_CELL_SIZE, UI_CELL_SIZE);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *label = lv_label_create(cell);
            if (NULL == label) {
                return false;
            }
            s_cell_labels[index] = label;
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_center(label);
        }
    }

    s_overlay = lv_obj_create(s_board);
    if (NULL == s_overlay) {
        return false;
    }
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_pos(s_overlay, UI_OVERLAY_X, UI_OVERLAY_Y);
    lv_obj_set_size(s_overlay, UI_OVERLAY_WIDTH, UI_OVERLAY_HEIGHT);
    lv_obj_set_style_radius(s_overlay, 6, 0);
    lv_obj_set_style_bg_color(s_overlay, color_hex(0x172326), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    s_overlay_label = create_label(s_overlay, 4, 15, UI_OVERLAY_WIDTH - 8, 28,
                                   &lv_font_montserrat_20,
                                   color_hex(0xFFFFFF), "");
    if (NULL == s_overlay_label) {
        return false;
    }

    s_gesture_layer = lv_obj_create(s_board);
    if (NULL == s_gesture_layer) {
        return false;
    }
    lv_obj_remove_style_all(s_gesture_layer);
    lv_obj_set_pos(s_gesture_layer, 0, 0);
    lv_obj_set_size(s_gesture_layer, UI_BOARD_SIZE, UI_BOARD_SIZE);
    lv_obj_set_style_bg_opa(s_gesture_layer, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_gesture_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(s_gesture_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_gesture_layer, gesture_event_cb, LV_EVENT_ALL, NULL);
    return true;
}

static bool create_side_panel(void)
{
    lv_obj_t *title = create_label(s_screen, UI_PANEL_CONTENT_X, 4,
                                   UI_PANEL_CONTENT_WIDTH, 28,
                                   &lv_font_montserrat_24,
                                   color_hex(0x223033), "2048");
    lv_obj_t *score_title = create_label(s_screen, UI_PANEL_CONTENT_X, 37,
                                         UI_PANEL_CONTENT_WIDTH, 14,
                                         &lv_font_montserrat_12,
                                         color_hex(0x627173), "SCORE");
    s_score_value = create_label(s_screen, UI_PANEL_CONTENT_X, 51,
                                 UI_PANEL_CONTENT_WIDTH, 22,
                                 &lv_font_montserrat_16,
                                 color_hex(0x1E2B2E), "0");
    lv_obj_t *best_title = create_label(s_screen, UI_PANEL_CONTENT_X, 78,
                                        UI_PANEL_CONTENT_WIDTH, 14,
                                        &lv_font_montserrat_12,
                                        color_hex(0x627173), "BEST");
    s_best_value = create_label(s_screen, UI_PANEL_CONTENT_X, 92,
                                UI_PANEL_CONTENT_WIDTH, 22,
                                &lv_font_montserrat_16,
                                color_hex(0x1E2B2E), "0");
    s_status_label = create_label(s_screen, UI_PANEL_CONTENT_X, 124,
                                  UI_PANEL_CONTENT_WIDTH, 48,
                                  &lv_font_montserrat_12,
                                  color_hex(0x2F6F65), "PLAY");
    s_undo_button = create_icon_button(s_screen, UI_PANEL_CONTENT_X,
                                       LV_SYMBOL_LEFT, undo_event_cb);
    lv_obj_t *restart_button = create_icon_button(
        s_screen, UI_PANEL_CONTENT_X + UI_BUTTON_SIZE + UI_BUTTON_GAP,
        LV_SYMBOL_REFRESH, restart_event_cb);

    return (NULL != title) && (NULL != score_title) &&
           (NULL != s_score_value) && (NULL != best_title) &&
           (NULL != s_best_value) && (NULL != s_status_label) &&
           (NULL != s_undo_button) && (NULL != restart_button);
}

bool game_2048_ui_create(const game_2048_ui_callbacks_t *callbacks)
{
    if ((NULL == callbacks) || (NULL == callbacks->on_direction) ||
        (NULL == callbacks->on_undo) || (NULL == callbacks->on_restart)) {
        return false;
    }

    memset(&s_callbacks, 0, sizeof(s_callbacks));
    s_callbacks = *callbacks;
    game_2048_gesture_reset(&s_gesture);
    s_overlay_state = UI_OVERLAY_NONE;
    s_screen = lv_scr_act();
    if (NULL == s_screen) {
        return false;
    }
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_screen, color_hex(0xF4F7F5), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (!create_board() || !create_side_panel()) {
        return false;
    }
    s_initialized = true;
    return true;
}

void game_2048_ui_render(const game_2048_board_t *board,
                         uint32_t best_score,
                         bool undo_available,
                         bool touch_available)
{
    if (!s_initialized || !game_2048_board_is_valid(board)) {
        return;
    }

    for (size_t index = 0U; index < GAME_2048_CELL_COUNT; ++index) {
        const uint8_t exponent = board->cells[index];
        char tile_text[UI_TILE_TEXT_LENGTH] = {0};
        format_tile(exponent, tile_text, sizeof(tile_text));
        lv_obj_set_style_bg_color(s_cells[index], tile_background(exponent), 0);
        lv_obj_set_style_text_font(s_cell_labels[index], tile_font(exponent), 0);
        lv_obj_set_style_text_color(s_cell_labels[index], tile_foreground(exponent), 0);
        lv_label_set_text(s_cell_labels[index], tile_text);
        lv_obj_center(s_cell_labels[index]);
    }

    set_score_text(s_score_value, board->score);
    set_score_text(s_best_value, best_score);
    if (!touch_available) {
        lv_label_set_text(s_status_label, "TOUCH\nERROR");
        lv_obj_set_style_text_color(s_status_label, color_hex(0xC2413A), 0);
    } else if (GAME_2048_STATE_LOST == board->state) {
        lv_label_set_text(s_status_label, "NO\nMOVES");
        lv_obj_set_style_text_color(s_status_label, color_hex(0xC2413A), 0);
    } else if (GAME_2048_STATE_WON == board->state) {
        lv_label_set_text(s_status_label, "WON");
        lv_obj_set_style_text_color(s_status_label, color_hex(0xA66A00), 0);
    } else {
        lv_label_set_text(s_status_label, "PLAY");
        lv_obj_set_style_text_color(s_status_label, color_hex(0x2F6F65), 0);
    }

    if (undo_available) {
        lv_obj_clear_state(s_undo_button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_undo_button, LV_STATE_DISABLED);
    }

    if (touch_available && (GAME_2048_STATE_LOST != board->state)) {
        lv_obj_add_flag(s_gesture_layer, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_flag(s_gesture_layer, LV_OBJ_FLAG_CLICKABLE);
        game_2048_gesture_end(&s_gesture);
    }

    if (GAME_2048_STATE_LOST == board->state) {
        cancel_win_timer();
        show_overlay("NO MOVES", UI_OVERLAY_LOST);
    } else if (UI_OVERLAY_LOST == s_overlay_state) {
        hide_overlay();
    } else if (!board->win_reported && (UI_OVERLAY_WIN == s_overlay_state)) {
        cancel_win_timer();
        hide_overlay();
    }
}

void game_2048_ui_show_win_notice(void)
{
    if (!s_initialized) {
        return;
    }
    cancel_win_timer();
    show_overlay("2048!", UI_OVERLAY_WIN);
    s_win_timer = lv_timer_create(win_timer_cb, UI_WIN_NOTICE_MS, NULL);
}
