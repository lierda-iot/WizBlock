#include "rc_video_yuv_scale.h"
#include "rc_lcd_color.h"

#include <stddef.h>

bool rc_video_scale_vyuy_to_ycbycr(const uint8_t *source,
                                   uint32_t source_width,
                                   uint32_t source_height,
                                   uint8_t *output,
                                   uint32_t output_width,
                                   uint32_t output_height)
{
    if (NULL == source || NULL == output ||
        0U == source_width || 0U == source_height ||
        0U == output_width || 0U == output_height ||
        0U != (source_width & 1U) || 0U != (output_width & 1U)) {
        return false;
    }

    const uint32_t source_macros = source_width / 2U;
    const uint32_t output_macros = output_width / 2U;
    const size_t source_stride = (size_t)source_width * 2U;
    size_t output_offset = 0U;

    for (uint32_t y = 0U; y < output_height; ++y) {
        const uint32_t source_y = (uint32_t)(((uint64_t)y * source_height) /
                                             output_height);
        const uint8_t *source_row = source + (size_t)source_y * source_stride;
        for (uint32_t macro = 0U; macro < output_macros; ++macro) {
            const uint32_t source_macro =
                (uint32_t)(((uint64_t)macro * source_macros) / output_macros);
            const uint8_t *vyuy = source_row + (size_t)source_macro * 4U;
            output[output_offset++] = vyuy[1];
            output[output_offset++] = vyuy[2];
            output[output_offset++] = vyuy[3];
            output[output_offset++] = vyuy[0];
        }
    }
    return true;
}

static uint8_t clamp_u8(int value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 255) {
        return 255U;
    }
    return (uint8_t)value;
}

/* Match camera_display_demo's validated RGB565_BE camera preview contract. */
static uint16_t vyuy_to_camera_rgb565_be(uint8_t y, uint8_t u, uint8_t v)
{
    int c = (int)y - 16;
    const int d = (int)u - 128;
    const int e = (int)v - 128;
    if (c < 0) {
        c = 0;
    }
    const uint8_t red = clamp_u8((298 * c + 409 * e + 128) >> 8);
    const uint8_t green = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    const uint8_t blue = clamp_u8((298 * c + 516 * d + 128) >> 8);
    const uint16_t rgb565 = (uint16_t)(((uint16_t)(red >> 3) << 11) |
                                       ((uint16_t)(green >> 2) << 5) |
                                       (uint16_t)(blue >> 3));
    return (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
}

bool rc_video_scale_vyuy_to_lcd_bgr565_region(const uint8_t *source,
                                             uint32_t source_width,
                                             uint32_t source_height,
                                             uint16_t *output,
                                             uint32_t output_width,
                                             uint32_t output_height,
                                             uint32_t output_y_start,
                                             uint32_t output_lines,
                                             uint32_t output_stride_pixels)
{
    if (NULL == source || NULL == output ||
        0U == source_width || 0U == source_height ||
        0U == output_width || 0U == output_height ||
        0U == output_lines || output_y_start >= output_height ||
        output_lines > (output_height - output_y_start) ||
        output_stride_pixels < output_width || 0U != (source_width & 1U)) {
        return false;
    }

    const size_t source_stride = (size_t)source_width * 2U;
    for (uint32_t row = 0U; row < output_lines; ++row) {
        const uint32_t y = output_y_start + row;
        const uint32_t source_y = (uint32_t)(((uint64_t)y * source_height) /
                                             output_height);
        const uint8_t *source_row = source + (size_t)source_y * source_stride;
        uint16_t *output_row = output + (size_t)row * output_stride_pixels;
        for (uint32_t x = 0U; x < output_width; ++x) {
            const uint32_t source_x = (uint32_t)(((uint64_t)x * source_width) /
                                                 output_width);
            const uint8_t *vyuy = source_row + (size_t)(source_x & ~1U) * 2U;
            const uint8_t luma = vyuy[(0U == (source_x & 1U)) ? 1U : 3U];
            output_row[x] = vyuy_to_camera_rgb565_be(luma, vyuy[2], vyuy[0]);
        }
    }
    return true;
}

bool rc_video_scale_vyuy_to_lcd_bgr565(const uint8_t *source,
                                       uint32_t source_width,
                                       uint32_t source_height,
                                       uint16_t *output,
                                       uint32_t output_width,
                                       uint32_t output_height,
                                       uint32_t output_stride_pixels)
{
    return rc_video_scale_vyuy_to_lcd_bgr565_region(
        source, source_width, source_height, output,
        output_width, output_height, 0U, output_height,
        output_stride_pixels);
}
