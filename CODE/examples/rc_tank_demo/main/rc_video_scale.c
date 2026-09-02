#include "rc_video_scale.h"

#include <stddef.h>

bool rc_video_scale_rgb565_nearest(const uint16_t *source,
                                   uint32_t source_width,
                                   uint32_t source_height,
                                   uint16_t *output,
                                   uint32_t output_width,
                                   uint32_t output_height)
{
    if (NULL == source || NULL == output ||
        0U == source_width || 0U == source_height ||
        0U == output_width || 0U == output_height) {
        return false;
    }

    for (uint32_t y = 0U; y < output_height; ++y) {
        const uint32_t source_y = (uint32_t)(((uint64_t)y * source_height) /
                                             output_height);
        for (uint32_t x = 0U; x < output_width; ++x) {
            const uint32_t source_x = (uint32_t)(((uint64_t)x * source_width) /
                                                 output_width);
            output[(size_t)y * output_width + x] =
                source[(size_t)source_y * source_width + source_x];
        }
    }
    return true;
}
