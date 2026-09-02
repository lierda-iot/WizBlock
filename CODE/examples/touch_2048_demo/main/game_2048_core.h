#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GAME_2048_BOARD_SIDE 4U
#define GAME_2048_CELL_COUNT (GAME_2048_BOARD_SIDE * GAME_2048_BOARD_SIDE)
#define GAME_2048_MAX_CELL_EXPONENT 31U
#define GAME_2048_WIN_EXPONENT 11U

typedef enum {
    GAME_2048_DIRECTION_UP = 0,
    GAME_2048_DIRECTION_DOWN,
    GAME_2048_DIRECTION_LEFT,
    GAME_2048_DIRECTION_RIGHT,
} game_2048_direction_t;

typedef enum {
    GAME_2048_STATE_PLAYING = 0,
    GAME_2048_STATE_WON,
    GAME_2048_STATE_LOST,
} game_2048_state_t;

typedef enum {
    GAME_2048_MOVE_ERROR = -1,
    GAME_2048_MOVE_NO_CHANGE = 0,
    GAME_2048_MOVE_CHANGED = 1,
} game_2048_move_result_t;

typedef uint32_t (*game_2048_rng_fn_t)(void *ctx);

typedef struct {
    uint8_t cells[GAME_2048_CELL_COUNT];
    uint32_t score;
    game_2048_state_t state;
    bool win_reported;
} game_2048_board_t;

typedef struct {
    game_2048_board_t board;
    bool valid;
} game_2048_snapshot_t;

bool game_2048_board_is_valid(const game_2048_board_t *board);
bool game_2048_new_game(game_2048_board_t *board, game_2048_rng_fn_t rng, void *rng_ctx);
game_2048_move_result_t game_2048_move(game_2048_board_t *board,
                                       game_2048_direction_t direction,
                                       game_2048_rng_fn_t rng,
                                       void *rng_ctx);
bool game_2048_can_move(const game_2048_board_t *board);
uint8_t game_2048_empty_count(const game_2048_board_t *board);
uint8_t game_2048_max_exponent(const game_2048_board_t *board);
void game_2048_snapshot_clear(game_2048_snapshot_t *snapshot);
bool game_2048_snapshot_save(game_2048_snapshot_t *snapshot,
                             const game_2048_board_t *board);
bool game_2048_snapshot_restore(game_2048_snapshot_t *snapshot,
                                game_2048_board_t *board);
