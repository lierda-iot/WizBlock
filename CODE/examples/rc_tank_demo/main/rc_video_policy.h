#pragma once

#include <stdbool.h>

static inline bool rc_video_controller_ready(const void *controller)
{
    return controller != 0;
}
