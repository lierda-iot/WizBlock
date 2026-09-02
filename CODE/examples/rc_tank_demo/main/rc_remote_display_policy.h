#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool presented;
    int knob_dx;
    int knob_dy;
    bool active;
    uint32_t network_epoch;
} rc_remote_display_policy_t;

static inline bool rc_remote_display_should_present(
    rc_remote_display_policy_t *policy,
    bool video_updated,
    int knob_dx,
    int knob_dy,
    bool active,
    uint32_t network_epoch)
{
    if (!policy) {
        return false;
    }

    const bool joystick_changed = !policy->presented ||
                                  policy->knob_dx != knob_dx ||
                                  policy->knob_dy != knob_dy ||
                                  policy->active != active;
    const bool network_changed = !policy->presented ||
                                 policy->network_epoch != network_epoch;
    if (!video_updated && !joystick_changed && !network_changed) {
        return false;
    }

    policy->presented = true;
    policy->knob_dx = knob_dx;
    policy->knob_dy = knob_dy;
    policy->active = active;
    policy->network_epoch = network_epoch;
    return true;
}
