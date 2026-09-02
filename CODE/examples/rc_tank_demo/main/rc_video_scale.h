#pragma once

#include <stdbool.h>
#include <stdint.h>

bool rc_video_scale_rgb565_nearest(const uint16_t *source,
                                   uint32_t source_width,
                                   uint32_t source_height,
                                   uint16_t *output,
                                   uint32_t output_width,
                                   uint32_t output_height);
