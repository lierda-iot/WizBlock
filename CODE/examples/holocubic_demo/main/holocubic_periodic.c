#include "holocubic_periodic.h"

#include <stddef.h>

bool holocubic_periodic_init(holocubic_periodic_t *periodic,
                             uint64_t now_ms,
                             uint32_t period_ms)
{
    if (NULL == periodic || 0U == period_ms ||
        UINT64_MAX - now_ms < period_ms) {
        return false;
    }

    periodic->next_deadline_ms = now_ms + period_ms;
    periodic->period_ms = period_ms;
    periodic->initialized = true;
    return true;
}

uint32_t holocubic_periodic_next_delay(holocubic_periodic_t *periodic,
                                       uint64_t now_ms)
{
    uint64_t target_ms = 0U;

    if (NULL == periodic || !periodic->initialized ||
        0U == periodic->period_ms) {
        return 0U;
    }

    target_ms = periodic->next_deadline_ms;
    if (now_ms > target_ms) {
        const uint64_t late_ms = now_ms - target_ms;
        const uint64_t skipped_periods =
            (late_ms / periodic->period_ms) +
            ((0U != (late_ms % periodic->period_ms)) ? 1U : 0U);
        if (skipped_periods >
            ((UINT64_MAX - target_ms) / periodic->period_ms)) {
            periodic->initialized = false;
            return 0U;
        }
        target_ms += skipped_periods * periodic->period_ms;
    }

    if (UINT64_MAX - target_ms < periodic->period_ms) {
        periodic->initialized = false;
        return 0U;
    }
    periodic->next_deadline_ms = target_ms + periodic->period_ms;
    return (now_ms < target_ms) ? (uint32_t)(target_ms - now_ms) : 0U;
}
