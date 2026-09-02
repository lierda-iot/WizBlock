#ifndef HOLOCUBIC_UI_CLOCK_H
#define HOLOCUBIC_UI_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t last_ms;
    bool initialized;
} holocubic_ui_clock_t;

void holocubic_ui_clock_init(holocubic_ui_clock_t *clock);
uint32_t holocubic_ui_clock_advance(holocubic_ui_clock_t *clock,
                                    uint64_t now_ms);

#endif
