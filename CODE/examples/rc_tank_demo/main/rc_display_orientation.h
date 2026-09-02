#pragma once

#include <stdbool.h>

typedef enum {
    RC_DISPLAY_ROLE_TANK = 0,
    RC_DISPLAY_ROLE_REMOTE,
} rc_display_role_t;

typedef struct {
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
} rc_display_orientation_t;

static inline rc_display_orientation_t rc_display_orientation_for_role(
    const rc_display_role_t role)
{
    (void)role;
    return (rc_display_orientation_t){
        .swap_xy = true,
        .mirror_x = false,
        .mirror_y = true,
    };
}
