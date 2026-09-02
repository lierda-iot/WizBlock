#include "holocubic_wifi_buffer_policy.h"

#include <stdint.h>

#define HOLO_WIFI_UI_WIDTH 320U
#define HOLO_WIFI_UI_PREFERRED_LINES 40U
#define HOLO_WIFI_UI_FALLBACK_LINES 16U
#define HOLO_WIFI_UI_BYTES_PER_PIXEL sizeof(uint16_t)

holocubic_wifi_buffer_policy_t holocubic_wifi_buffer_policy_default(void)
{
    const holocubic_wifi_buffer_policy_t policy = {
        .width = HOLO_WIFI_UI_WIDTH,
        .preferred_lines = HOLO_WIFI_UI_PREFERRED_LINES,
        .fallback_lines = HOLO_WIFI_UI_FALLBACK_LINES,
        .bytes_per_pixel = HOLO_WIFI_UI_BYTES_PER_PIXEL,
    };
    return policy;
}

bool holocubic_wifi_buffer_plan_mode(
    const holocubic_wifi_buffer_policy_t *policy,
    bool double_buffer,
    holocubic_wifi_buffer_plan_t *plan)
{
    if (NULL == policy || NULL == plan || 0U == policy->width ||
        0U == policy->preferred_lines || 0U == policy->fallback_lines ||
        0U == policy->bytes_per_pixel) {
        return false;
    }

    *plan = (holocubic_wifi_buffer_plan_t){0};
    plan->valid = true;
    plan->double_buffer = double_buffer;
    plan->lines = double_buffer ? policy->preferred_lines :
                                  policy->fallback_lines;
    plan->pixels = (size_t)policy->width * plan->lines;
    plan->bytes_per_buffer = plan->pixels * policy->bytes_per_pixel;
    return true;
}
