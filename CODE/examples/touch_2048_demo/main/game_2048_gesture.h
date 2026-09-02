#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GAME_2048_GESTURE_THRESHOLD_PX 30

typedef enum {
    GAME_2048_GESTURE_NONE = 0,
    GAME_2048_GESTURE_UP,
    GAME_2048_GESTURE_DOWN,
    GAME_2048_GESTURE_LEFT,
    GAME_2048_GESTURE_RIGHT,
} game_2048_gesture_event_t;

typedef struct {
    int16_t start_x;
    int16_t start_y;
    bool active;
    bool locked;
} game_2048_gesture_t;

void game_2048_gesture_reset(game_2048_gesture_t *gesture);
bool game_2048_gesture_begin(game_2048_gesture_t *gesture, int16_t x, int16_t y);
game_2048_gesture_event_t game_2048_gesture_update(game_2048_gesture_t *gesture,
                                                   int16_t x,
                                                   int16_t y);
void game_2048_gesture_end(game_2048_gesture_t *gesture);
