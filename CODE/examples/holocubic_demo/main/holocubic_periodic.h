#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t next_deadline_ms;
    uint32_t period_ms;
    bool initialized;
} holocubic_periodic_t;

bool holocubic_periodic_init(holocubic_periodic_t *periodic,
                             uint64_t now_ms,
                             uint32_t period_ms);
uint32_t holocubic_periodic_next_delay(holocubic_periodic_t *periodic,
                                       uint64_t now_ms);
