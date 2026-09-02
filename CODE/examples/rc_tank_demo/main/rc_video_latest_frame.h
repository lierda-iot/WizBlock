#pragma once

#include <stdint.h>

/*
 * Select the slot to refill for a latest-frame pipeline.
 * free_mask and ready_mask contain one bit per slot; a negative result means
 * that the display task owns the only remaining slot.
 */
static inline int rc_video_latest_frame_select_slot(uint32_t free_mask,
                                                    uint32_t ready_mask)
{
    for (uint32_t bit = 0U; bit < 32U; ++bit) {
        if ((free_mask & (1UL << bit)) != 0U) {
            return (int)bit;
        }
    }
    for (uint32_t bit = 0U; bit < 32U; ++bit) {
        if ((ready_mask & (1UL << bit)) != 0U) {
            return (int)bit;
        }
    }
    return -1;
}
