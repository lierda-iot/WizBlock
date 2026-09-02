#include "game_2048_gesture.h"

#include <assert.h>
#include <stdio.h>

static void test_directions_and_single_event(void)
{
    const struct {
        int16_t x;
        int16_t y;
        game_2048_gesture_event_t event;
    } cases[] = {
        {100, 60, GAME_2048_GESTURE_RIGHT},
        {20, 60, GAME_2048_GESTURE_LEFT},
        {60, 20, GAME_2048_GESTURE_UP},
        {60, 100, GAME_2048_GESTURE_DOWN},
    };

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        game_2048_gesture_t gesture = {0};
        assert(game_2048_gesture_begin(&gesture, 60, 60));
        assert(cases[index].event == game_2048_gesture_update(&gesture,
                                                              cases[index].x,
                                                              cases[index].y));
        assert(GAME_2048_GESTURE_NONE == game_2048_gesture_update(&gesture,
                                                                   cases[index].x + 20,
                                                                   cases[index].y + 20));
        game_2048_gesture_end(&gesture);
    }
}

static void test_threshold_and_diagonal_tie(void)
{
    game_2048_gesture_t gesture = {0};

    assert(game_2048_gesture_begin(&gesture, 50, 50));
    assert(GAME_2048_GESTURE_NONE == game_2048_gesture_update(&gesture, 79, 50));
    assert(GAME_2048_GESTURE_RIGHT == game_2048_gesture_update(&gesture, 80, 50));
    game_2048_gesture_end(&gesture);

    assert(game_2048_gesture_begin(&gesture, 50, 50));
    assert(GAME_2048_GESTURE_NONE == game_2048_gesture_update(&gesture, 80, 80));
    game_2048_gesture_end(&gesture);
}

static void test_lifecycle(void)
{
    game_2048_gesture_t gesture = {0};

    assert(game_2048_gesture_begin(&gesture, 1, 2));
    assert(!game_2048_gesture_begin(&gesture, 3, 4));
    game_2048_gesture_end(&gesture);
    assert(!gesture.active);
    assert(game_2048_gesture_begin(&gesture, 3, 4));
    game_2048_gesture_reset(&gesture);
    assert(!gesture.active);
    assert(!gesture.locked);
}

int main(void)
{
    test_directions_and_single_event();
    test_threshold_and_diagonal_tie();
    test_lifecycle();
    puts("game_2048_gesture_test: PASS");
    return 0;
}
