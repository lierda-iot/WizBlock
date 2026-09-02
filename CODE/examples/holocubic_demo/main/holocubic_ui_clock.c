#include "holocubic_ui_clock.h"

#include <limits.h>
#include <stddef.h>

void holocubic_ui_clock_init(holocubic_ui_clock_t *clock)
{
    if (NULL == clock) {
        return;
    }
    *clock = (holocubic_ui_clock_t){0};
}

uint32_t holocubic_ui_clock_advance(holocubic_ui_clock_t *clock,
                                    uint64_t now_ms)
{
    uint64_t elapsed_ms = 0U;

    if (NULL == clock) {
        return 0U;
    }
    if (!clock->initialized || now_ms < clock->last_ms) {
        clock->last_ms = now_ms;
        clock->initialized = true;
        return 0U;
    }

    elapsed_ms = now_ms - clock->last_ms;
    clock->last_ms = now_ms;
    return (elapsed_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)elapsed_ms;
}
