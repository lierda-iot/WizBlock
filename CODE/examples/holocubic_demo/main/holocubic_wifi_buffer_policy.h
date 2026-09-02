#ifndef HOLOCUBIC_WIFI_BUFFER_POLICY_H
#define HOLOCUBIC_WIFI_BUFFER_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t preferred_lines;
    uint32_t fallback_lines;
    size_t bytes_per_pixel;
} holocubic_wifi_buffer_policy_t;

typedef struct {
    bool valid;
    bool double_buffer;
    uint32_t lines;
    size_t pixels;
    size_t bytes_per_buffer;
} holocubic_wifi_buffer_plan_t;

holocubic_wifi_buffer_policy_t holocubic_wifi_buffer_policy_default(void);
bool holocubic_wifi_buffer_plan_mode(
    const holocubic_wifi_buffer_policy_t *policy,
    bool double_buffer,
    holocubic_wifi_buffer_plan_t *plan);

#endif
