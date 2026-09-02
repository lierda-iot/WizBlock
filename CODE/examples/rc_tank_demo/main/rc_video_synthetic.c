#include "rc_video_synthetic.h"

#include <stddef.h>

static void write_macro_pixel(uint8_t *output,
                              uint32_t macro_index,
                              uint8_t y0,
                              uint8_t y1,
                              uint8_t cb,
                              uint8_t cr)
{
    output[macro_index * 4U + 0U] = y0;
    output[macro_index * 4U + 1U] = cb;
    output[macro_index * 4U + 2U] = y1;
    output[macro_index * 4U + 3U] = cr;
}

bool rc_video_synthetic_fill_ycbycr(uint8_t *output,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t frame_index)
{
    if (NULL == output || 0U == width || 0U == height || 0U != (width & 1U)) {
        return false;
    }

    const uint32_t macros_per_row = width / 2U;
    /* 每 8 帧轮换一次四种色块相位，便于用相邻帧颜色边界观察撕裂。 */
    const uint32_t color_phase = (frame_index / 8U) & 3U;
    const uint32_t marker_x = (frame_index * 3U) % width;
    const uint32_t marker_y = (frame_index * 2U) % height;
    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x < width; x += 2U) {
            const uint32_t block_x = x / RC_SYNTHETIC_VIDEO_BLOCK_W;
            const uint32_t block_y = y / RC_SYNTHETIC_VIDEO_BLOCK_H;
            const uint32_t color_index = (block_x + block_y + color_phase) & 3U;
            uint8_t y0 = 40U;
            uint8_t y1 = 40U;
            uint8_t cb = 128U;
            uint8_t cr = 128U;
            if (0U == color_index) {
                y0 = 190U; y1 = 190U; cr = 210U;
            } else if (1U == color_index) {
                y0 = 180U; y1 = 180U; cb = 210U;
            } else if (2U == color_index) {
                y0 = 180U; y1 = 180U; cb = 75U;
            } else {
                y0 = 210U; y1 = 210U; cr = 75U;
            }

            const bool border = (0U == x || 0U == y ||
                                 x + 2U >= width || y + 1U >= height);
            const bool marker = (x <= marker_x && marker_x < x + 2U &&
                                 y <= marker_y && marker_y < y + 1U);
            if (border || marker) {
                y0 = 235U; y1 = 235U; cb = 128U; cr = 128U;
            }
            write_macro_pixel(output,
                              y * macros_per_row + (x / 2U),
                              y0, y1, cb, cr);
        }
    }
    return true;
}
