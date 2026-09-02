#pragma once

#include "game_2048_core.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*game_2048_ui_direction_cb_t)(void *ctx,
                                             game_2048_direction_t direction);
typedef void (*game_2048_ui_action_cb_t)(void *ctx);

typedef struct {
    game_2048_ui_direction_cb_t on_direction;
    game_2048_ui_action_cb_t on_undo;
    game_2048_ui_action_cb_t on_restart;
    game_2048_ui_action_cb_t on_touch_rejected;
    void *ctx;
} game_2048_ui_callbacks_t;

bool game_2048_ui_create(const game_2048_ui_callbacks_t *callbacks);
void game_2048_ui_render(const game_2048_board_t *board,
                         uint32_t best_score,
                         bool undo_available,
                         bool touch_available);
void game_2048_ui_show_win_notice(void);
