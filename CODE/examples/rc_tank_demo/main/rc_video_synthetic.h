#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RC_SYNTHETIC_VIDEO_BLOCK_W 40U
#define RC_SYNTHETIC_VIDEO_BLOCK_H 30U

bool rc_video_synthetic_fill_ycbycr(uint8_t *output,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t frame_index);

