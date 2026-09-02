/*
 * The move/merge structure is informed by mevdschee/2048.c at commit
 * afc8898691f54d43309497f4c32682fe90bb5f57 (MIT). This implementation
 * replaces terminal, random and score handling for the local API.
 * See ../THIRD_PARTY_NOTICES.md.
 */

#include "game_2048_core.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define GAME_2048_NEW_TILE_ROLL_BOUND 10U
#define GAME_2048_NEW_TILE_FOUR_ROLL 9U
#define GAME_2048_RNG_MAX_ATTEMPTS 64U

static bool is_direction_valid(game_2048_direction_t direction)
{
    return (GAME_2048_DIRECTION_UP == direction) ||
           (GAME_2048_DIRECTION_DOWN == direction) ||
           (GAME_2048_DIRECTION_LEFT == direction) ||
           (GAME_2048_DIRECTION_RIGHT == direction);
}

static size_t get_cell_index(game_2048_direction_t direction,
                             size_t line,
                             size_t offset)
{
    switch (direction) {
    case GAME_2048_DIRECTION_UP:
        return (offset * GAME_2048_BOARD_SIDE) + line;
    case GAME_2048_DIRECTION_DOWN:
        return ((GAME_2048_BOARD_SIDE - 1U - offset) * GAME_2048_BOARD_SIDE) + line;
    case GAME_2048_DIRECTION_LEFT:
        return (line * GAME_2048_BOARD_SIDE) + offset;
    case GAME_2048_DIRECTION_RIGHT:
        return (line * GAME_2048_BOARD_SIDE) +
               (GAME_2048_BOARD_SIDE - 1U - offset);
    default:
        return 0U;
    }
}

static uint32_t add_saturated(uint32_t left, uint32_t right)
{
    if (UINT32_MAX - left < right) {
        return UINT32_MAX;
    }
    return left + right;
}

static bool random_bounded(game_2048_rng_fn_t rng,
                           void *rng_ctx,
                           uint32_t bound,
                           uint32_t *value)
{
    if ((NULL == rng) || (NULL == value) || (0U == bound)) {
        return false;
    }

    const uint32_t threshold = (uint32_t)(0U - bound) % bound;
    for (uint32_t attempt = 0U; attempt < GAME_2048_RNG_MAX_ATTEMPTS; ++attempt) {
        const uint32_t sample = rng(rng_ctx);
        if (sample >= threshold) {
            *value = sample % bound;
            return true;
        }
    }
    return false;
}

static bool add_random_tile(game_2048_board_t *board,
                            game_2048_rng_fn_t rng,
                            void *rng_ctx)
{
    const uint8_t empty_count = game_2048_empty_count(board);
    uint32_t empty_ordinal = 0U;
    uint32_t tile_roll = 0U;

    if ((0U == empty_count) ||
        !random_bounded(rng, rng_ctx, empty_count, &empty_ordinal) ||
        !random_bounded(rng, rng_ctx, GAME_2048_NEW_TILE_ROLL_BOUND, &tile_roll)) {
        return false;
    }

    for (size_t index = 0U; index < GAME_2048_CELL_COUNT; ++index) {
        if (0U != board->cells[index]) {
            continue;
        }
        if (0U == empty_ordinal) {
            board->cells[index] = (GAME_2048_NEW_TILE_FOUR_ROLL == tile_roll) ? 2U : 1U;
            return true;
        }
        --empty_ordinal;
    }
    return false;
}

static bool process_line(game_2048_board_t *board,
                         game_2048_direction_t direction,
                         size_t line)
{
    uint8_t compact[GAME_2048_BOARD_SIDE] = {0};
    uint8_t merged[GAME_2048_BOARD_SIDE] = {0};
    size_t compact_count = 0U;
    size_t merged_count = 0U;

    for (size_t offset = 0U; offset < GAME_2048_BOARD_SIDE; ++offset) {
        const uint8_t exponent = board->cells[get_cell_index(direction, line, offset)];
        if (0U != exponent) {
            compact[compact_count++] = exponent;
        }
    }

    size_t index = 0U;
    while (index < compact_count) {
        const uint8_t exponent = compact[index];
        if ((index + 1U < compact_count) &&
            (exponent == compact[index + 1U]) &&
            (GAME_2048_MAX_CELL_EXPONENT > exponent)) {
            const uint8_t merged_exponent = (uint8_t)(exponent + 1U);
            const uint32_t merged_value = UINT32_C(1) << merged_exponent;
            merged[merged_count++] = merged_exponent;
            board->score = add_saturated(board->score, merged_value);
            index += 2U;
        } else {
            merged[merged_count++] = exponent;
            ++index;
        }
    }

    bool changed = false;
    for (size_t offset = 0U; offset < GAME_2048_BOARD_SIDE; ++offset) {
        const size_t cell_index = get_cell_index(direction, line, offset);
        const uint8_t next_exponent = (offset < merged_count) ? merged[offset] : 0U;
        if (board->cells[cell_index] != next_exponent) {
            board->cells[cell_index] = next_exponent;
            changed = true;
        }
    }
    return changed;
}

static void update_state(game_2048_board_t *board)
{
    if (!game_2048_can_move(board)) {
        board->state = GAME_2048_STATE_LOST;
        return;
    }

    if (!board->win_reported &&
        (GAME_2048_WIN_EXPONENT <= game_2048_max_exponent(board))) {
        board->win_reported = true;
        board->state = GAME_2048_STATE_WON;
    }
}

bool game_2048_board_is_valid(const game_2048_board_t *board)
{
    if (NULL == board) {
        return false;
    }
    if ((GAME_2048_STATE_PLAYING != board->state) &&
        (GAME_2048_STATE_WON != board->state) &&
        (GAME_2048_STATE_LOST != board->state)) {
        return false;
    }
    for (size_t index = 0U; index < GAME_2048_CELL_COUNT; ++index) {
        if (GAME_2048_MAX_CELL_EXPONENT < board->cells[index]) {
            return false;
        }
    }
    return true;
}

bool game_2048_new_game(game_2048_board_t *board, game_2048_rng_fn_t rng, void *rng_ctx)
{
    if ((NULL == board) || (NULL == rng)) {
        return false;
    }

    game_2048_board_t next = {
        .cells = {0},
        .score = 0U,
        .state = GAME_2048_STATE_PLAYING,
        .win_reported = false,
    };
    if (!add_random_tile(&next, rng, rng_ctx) ||
        !add_random_tile(&next, rng, rng_ctx)) {
        return false;
    }
    *board = next;
    return true;
}

game_2048_move_result_t game_2048_move(game_2048_board_t *board,
                                       game_2048_direction_t direction,
                                       game_2048_rng_fn_t rng,
                                       void *rng_ctx)
{
    if ((NULL == board) || (NULL == rng) ||
        !is_direction_valid(direction) ||
        !game_2048_board_is_valid(board)) {
        return GAME_2048_MOVE_ERROR;
    }
    if (GAME_2048_STATE_LOST == board->state) {
        return GAME_2048_MOVE_NO_CHANGE;
    }

    game_2048_board_t next = *board;
    bool changed = false;
    for (size_t line = 0U; line < GAME_2048_BOARD_SIDE; ++line) {
        changed = process_line(&next, direction, line) || changed;
    }

    if (!changed) {
        update_state(&next);
        *board = next;
        return GAME_2048_MOVE_NO_CHANGE;
    }
    if (!add_random_tile(&next, rng, rng_ctx)) {
        return GAME_2048_MOVE_ERROR;
    }

    update_state(&next);
    *board = next;
    return GAME_2048_MOVE_CHANGED;
}

bool game_2048_can_move(const game_2048_board_t *board)
{
    if (!game_2048_board_is_valid(board)) {
        return false;
    }

    for (size_t row = 0U; row < GAME_2048_BOARD_SIDE; ++row) {
        for (size_t column = 0U; column < GAME_2048_BOARD_SIDE; ++column) {
            const size_t index = (row * GAME_2048_BOARD_SIDE) + column;
            const uint8_t exponent = board->cells[index];
            if (0U == exponent) {
                return true;
            }
            if ((column + 1U < GAME_2048_BOARD_SIDE) &&
                (exponent == board->cells[index + 1U]) &&
                (GAME_2048_MAX_CELL_EXPONENT > exponent)) {
                return true;
            }
            if ((row + 1U < GAME_2048_BOARD_SIDE) &&
                (exponent == board->cells[index + GAME_2048_BOARD_SIDE]) &&
                (GAME_2048_MAX_CELL_EXPONENT > exponent)) {
                return true;
            }
        }
    }
    return false;
}

uint8_t game_2048_empty_count(const game_2048_board_t *board)
{
    if (NULL == board) {
        return 0U;
    }

    uint8_t count = 0U;
    for (size_t index = 0U; index < GAME_2048_CELL_COUNT; ++index) {
        if (0U == board->cells[index]) {
            ++count;
        }
    }
    return count;
}

uint8_t game_2048_max_exponent(const game_2048_board_t *board)
{
    if (NULL == board) {
        return 0U;
    }

    uint8_t maximum = 0U;
    for (size_t index = 0U; index < GAME_2048_CELL_COUNT; ++index) {
        if (maximum < board->cells[index]) {
            maximum = board->cells[index];
        }
    }
    return maximum;
}

void game_2048_snapshot_clear(game_2048_snapshot_t *snapshot)
{
    if (NULL != snapshot) {
        memset(snapshot, 0, sizeof(*snapshot));
    }
}

bool game_2048_snapshot_save(game_2048_snapshot_t *snapshot,
                             const game_2048_board_t *board)
{
    if ((NULL == snapshot) || !game_2048_board_is_valid(board)) {
        return false;
    }
    snapshot->board = *board;
    snapshot->valid = true;
    return true;
}

bool game_2048_snapshot_restore(game_2048_snapshot_t *snapshot,
                                game_2048_board_t *board)
{
    if ((NULL == snapshot) || (NULL == board) || !snapshot->valid ||
        !game_2048_board_is_valid(&snapshot->board)) {
        return false;
    }
    *board = snapshot->board;
    snapshot->valid = false;
    return true;
}
