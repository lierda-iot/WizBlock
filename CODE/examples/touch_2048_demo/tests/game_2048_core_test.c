#include "game_2048_core.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint32_t *values;
    size_t count;
    size_t index;
} test_rng_t;

static uint32_t next_rng(void *ctx)
{
    test_rng_t *rng = (test_rng_t *)ctx;
    assert(NULL != rng);
    assert(rng->index < rng->count);
    return rng->values[rng->index++];
}

static void test_new_game_and_score(void)
{
    const uint32_t random_values[] = {0U, 10U, 15U, 10U};
    test_rng_t rng = {random_values, 4U, 0U};
    game_2048_board_t board = {0};

    assert(game_2048_new_game(&board, next_rng, &rng));
    assert(14U == game_2048_empty_count(&board));
    assert(0U == board.score);
    assert(GAME_2048_STATE_PLAYING == board.state);
    assert(game_2048_board_is_valid(&board));
}

static void test_left_merge_is_single_pass(void)
{
    const uint32_t random_values[] = {14U, 10U};
    const uint8_t initial[] = {1U, 1U, 1U, 1U, 0U, 0U, 0U, 0U,
                               0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t expected_prefix[] = {2U, 2U, 1U, 0U};
    test_rng_t rng = {random_values, 2U, 0U};
    game_2048_board_t board = {
        .cells = {0},
        .score = 0U,
        .state = GAME_2048_STATE_PLAYING,
        .win_reported = false,
    };
    memcpy(board.cells, initial, sizeof(initial));

    assert(GAME_2048_MOVE_CHANGED == game_2048_move(&board,
                                                     GAME_2048_DIRECTION_LEFT,
                                                     next_rng,
                                                     &rng));
    assert(8U == board.score);
    assert(2U == board.cells[0]);
    assert(2U == board.cells[1]);
    assert(1U == board.cells[2]);
    assert(0U == board.cells[3]);
    assert(13U == game_2048_empty_count(&board));
    assert(0 == memcmp(board.cells, expected_prefix, sizeof(expected_prefix)));
    assert(2U == rng.index);
}

static void test_merge_patterns_and_gaps(void)
{
    const struct {
        uint8_t initial[GAME_2048_BOARD_SIDE];
        uint8_t expected[GAME_2048_BOARD_SIDE];
        uint32_t expected_score;
    } cases[] = {
        {{1U, 1U, 2U, 0U}, {2U, 2U, 0U, 0U}, 4U},
        {{2U, 2U, 3U, 0U}, {3U, 3U, 0U, 0U}, 8U},
        {{1U, 0U, 1U, 1U}, {2U, 1U, 0U, 0U}, 4U},
    };

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const uint32_t random_values[] = {13U, 10U};
        test_rng_t rng = {random_values, 2U, 0U};
        game_2048_board_t board = {
            .cells = {0},
            .score = 0U,
            .state = GAME_2048_STATE_PLAYING,
            .win_reported = false,
        };
        memcpy(board.cells, cases[index].initial, GAME_2048_BOARD_SIDE);

        assert(GAME_2048_MOVE_CHANGED == game_2048_move(&board,
                                                         GAME_2048_DIRECTION_LEFT,
                                                         next_rng,
                                                         &rng));
        assert(0 == memcmp(board.cells, cases[index].expected, GAME_2048_BOARD_SIDE));
        assert(1U == board.cells[GAME_2048_CELL_COUNT - 1U]);
        assert(cases[index].expected_score == board.score);
    }
}

static void test_random_four_and_rejection_sampling(void)
{
    const uint32_t random_values[] = {
        15U,
        0U, 1U, 2U, 3U, 4U, 5U, 9U,
        0U, 1U,
        6U,
    };
    test_rng_t rng = {random_values, sizeof(random_values) / sizeof(random_values[0]), 0U};
    game_2048_board_t board = {0};

    assert(game_2048_new_game(&board, next_rng, &rng));
    assert(2U == board.cells[GAME_2048_CELL_COUNT - 1U]);
    assert(1U == board.cells[1]);
    assert(rng.count == rng.index);
}

static void test_rng_failure_is_atomic(void)
{
    uint32_t random_values[64] = {0};
    test_rng_t rng = {random_values, 64U, 0U};
    game_2048_board_t board = {
        .cells = {1U, 1U, 0U, 0U},
        .score = 7U,
        .state = GAME_2048_STATE_PLAYING,
        .win_reported = false,
    };
    const game_2048_board_t before = board;

    assert(GAME_2048_MOVE_ERROR == game_2048_move(&board,
                                                   GAME_2048_DIRECTION_LEFT,
                                                   next_rng,
                                                   &rng));
    assert(0 == memcmp(&before, &board, sizeof(board)));
    assert(64U == rng.index);
}

static void test_invalid_inputs(void)
{
    const uint32_t random_values[] = {15U, 10U};
    test_rng_t rng = {random_values, 2U, 0U};
    game_2048_board_t board = {
        .cells = {1U},
        .score = 0U,
        .state = GAME_2048_STATE_PLAYING,
        .win_reported = false,
    };
    const game_2048_board_t before = board;

    assert(GAME_2048_MOVE_ERROR == game_2048_move(&board,
                                                   (game_2048_direction_t)99,
                                                   next_rng,
                                                   &rng));
    assert(0 == memcmp(&before, &board, sizeof(board)));
    assert(0U == rng.index);
    board.cells[0] = GAME_2048_MAX_CELL_EXPONENT + 1U;
    assert(!game_2048_board_is_valid(&board));
}

static void test_all_directions(void)
{
    const uint32_t random_values[] = {15U, 10U, 15U, 10U, 15U, 10U};
    const uint8_t horizontal[] = {1U, 1U, 0U, 0U, 0U, 0U, 0U, 0U,
                                  0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t vertical[] = {1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U,
                                0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    test_rng_t rng = {random_values, 8U, 0U};
    game_2048_board_t board = {0};

    memcpy(board.cells, horizontal, sizeof(horizontal));
    board.state = GAME_2048_STATE_PLAYING;
    assert(GAME_2048_MOVE_CHANGED == game_2048_move(&board,
                                                     GAME_2048_DIRECTION_RIGHT,
                                                     next_rng,
                                                     &rng));
    assert(2U == board.cells[3]);
    assert(1U == board.cells[0]);

    memset(&board, 0, sizeof(board));
    memcpy(board.cells, vertical, sizeof(vertical));
    board.state = GAME_2048_STATE_PLAYING;
    assert(GAME_2048_MOVE_CHANGED == game_2048_move(&board,
                                                     GAME_2048_DIRECTION_DOWN,
                                                     next_rng,
                                                     &rng));
    assert(2U == board.cells[12]);
    assert(1U == board.cells[0]);

    memset(&board, 0, sizeof(board));
    memcpy(board.cells, vertical, sizeof(vertical));
    board.state = GAME_2048_STATE_PLAYING;
    assert(GAME_2048_MOVE_CHANGED == game_2048_move(&board,
                                                     GAME_2048_DIRECTION_UP,
                                                     next_rng,
                                                     &rng));
    assert(2U == board.cells[0]);
    assert(1U == board.cells[1]);
}

static void test_no_change_does_not_consume_rng_or_score(void)
{
    const uint32_t random_values[] = {0U, 0U};
    const uint8_t initial[] = {1U, 2U, 1U, 2U, 2U, 1U, 2U, 1U,
                               1U, 2U, 1U, 2U, 2U, 1U, 2U, 1U};
    test_rng_t rng = {random_values, 2U, 0U};
    game_2048_board_t board = {
        .cells = {0},
        .score = 42U,
        .state = GAME_2048_STATE_PLAYING,
        .win_reported = false,
    };
    game_2048_board_t before = board;
    memcpy(board.cells, initial, sizeof(initial));
    before = board;

    assert(GAME_2048_MOVE_NO_CHANGE == game_2048_move(&board,
                                                      GAME_2048_DIRECTION_LEFT,
                                                      next_rng,
                                                      &rng));
    assert(0 == memcmp(before.cells, board.cells, sizeof(board.cells)));
    assert(before.score == board.score);
    assert(GAME_2048_STATE_LOST == board.state);
    assert(0U == rng.index);
}

static void test_score_saturates_and_exponent_31_does_not_merge(void)
{
    const uint32_t random_values[] = {15U, 10U};
    test_rng_t rng = {random_values, 2U, 0U};
    game_2048_board_t board = {
        .cells = {30U, 30U, 0U, 0U},
        .score = UINT32_MAX - 1U,
        .state = GAME_2048_STATE_PLAYING,
        .win_reported = true,
    };

    assert(GAME_2048_MOVE_CHANGED == game_2048_move(&board,
                                                    GAME_2048_DIRECTION_LEFT,
                                                    next_rng,
                                                    &rng));
    assert(UINT32_MAX == board.score);
    assert(31U == board.cells[0]);

    memset(&board, 0, sizeof(board));
    board.cells[0] = 31U;
    board.cells[1] = 31U;
    board.state = GAME_2048_STATE_PLAYING;
    assert(GAME_2048_MOVE_NO_CHANGE == game_2048_move(&board,
                                                      GAME_2048_DIRECTION_LEFT,
                                                      next_rng,
                                                      &rng));
    assert(31U == board.cells[0]);
    assert(31U == board.cells[1]);
}

static void test_win_and_loss_state(void)
{
    const uint32_t random_values[] = {15U, 10U};
    test_rng_t rng = {random_values, 2U, 0U};
    game_2048_board_t board = {
        .cells = {10U, 10U, 0U, 0U},
        .score = 0U,
        .state = GAME_2048_STATE_PLAYING,
        .win_reported = false,
    };

    assert(GAME_2048_MOVE_CHANGED == game_2048_move(&board,
                                                     GAME_2048_DIRECTION_LEFT,
                                                     next_rng,
                                                     &rng));
    assert(GAME_2048_STATE_WON == board.state);
    assert(board.win_reported);
}

static void test_snapshot(void)
{
    game_2048_snapshot_t snapshot = {0};
    game_2048_board_t board = {
        .cells = {1U, 2U},
        .score = 9U,
        .state = GAME_2048_STATE_PLAYING,
        .win_reported = false,
    };
    const game_2048_board_t expected = board;
    game_2048_board_t restored = {0};

    assert(game_2048_snapshot_save(&snapshot, &board));
    board.score = 100U;
    assert(game_2048_snapshot_restore(&snapshot, &restored));
    assert(0 == memcmp(&expected, &restored, sizeof(restored)));
    assert(!snapshot.valid);
}

int main(void)
{
    test_new_game_and_score();
    test_left_merge_is_single_pass();
    test_merge_patterns_and_gaps();
    test_random_four_and_rejection_sampling();
    test_rng_failure_is_atomic();
    test_invalid_inputs();
    test_all_directions();
    test_no_change_does_not_consume_rng_or_score();
    test_score_saturates_and_exponent_31_does_not_merge();
    test_win_and_loss_state();
    test_snapshot();
    puts("game_2048_core_test: PASS");
    return 0;
}
