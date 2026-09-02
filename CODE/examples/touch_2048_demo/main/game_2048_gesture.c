#include "game_2048_gesture.h"

#include <stddef.h>
#include <string.h>

static int32_t absolute_value(int32_t value)
{
    return (0 > value) ? -value : value;
}

void game_2048_gesture_reset(game_2048_gesture_t *gesture)
{
    if (NULL != gesture) {
        memset(gesture, 0, sizeof(*gesture));
    }
}

bool game_2048_gesture_begin(game_2048_gesture_t *gesture, int16_t x, int16_t y)
{
    if ((NULL == gesture) || gesture->active) {
        return false;
    }
    gesture->start_x = x;
    gesture->start_y = y;
    gesture->active = true;
    gesture->locked = false;
    return true;
}

game_2048_gesture_event_t game_2048_gesture_update(game_2048_gesture_t *gesture,
                                                   int16_t x,
                                                   int16_t y)
{
    if ((NULL == gesture) || !gesture->active || gesture->locked) {
        return GAME_2048_GESTURE_NONE;
    }

    const int32_t delta_x = (int32_t)x - gesture->start_x;
    const int32_t delta_y = (int32_t)y - gesture->start_y;
    const int32_t distance_x = absolute_value(delta_x);
    const int32_t distance_y = absolute_value(delta_y);

    if (distance_x == distance_y) {
        return GAME_2048_GESTURE_NONE;
    }
    if ((distance_x > distance_y) &&
        (GAME_2048_GESTURE_THRESHOLD_PX <= distance_x)) {
        gesture->locked = true;
        return (0 > delta_x) ? GAME_2048_GESTURE_LEFT : GAME_2048_GESTURE_RIGHT;
    }
    if ((distance_y > distance_x) &&
        (GAME_2048_GESTURE_THRESHOLD_PX <= distance_y)) {
        gesture->locked = true;
        return (0 > delta_y) ? GAME_2048_GESTURE_UP : GAME_2048_GESTURE_DOWN;
    }
    return GAME_2048_GESTURE_NONE;
}

void game_2048_gesture_end(game_2048_gesture_t *gesture)
{
    game_2048_gesture_reset(gesture);
}
