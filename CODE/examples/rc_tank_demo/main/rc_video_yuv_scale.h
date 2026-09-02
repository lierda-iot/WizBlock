#pragma once

#include <stdbool.h>
#include <stdint.h>

bool rc_video_scale_vyuy_to_ycbycr(const uint8_t *source,
                                   uint32_t source_width,
                                   uint32_t source_height,
                                   uint8_t *output,
                                   uint32_t output_width,
                                   uint32_t output_height);

bool rc_video_scale_vyuy_to_lcd_bgr565(const uint8_t *source,
                                       uint32_t source_width,
                                       uint32_t source_height,
                                       uint16_t *output,
                                       uint32_t output_width,
                                       uint32_t output_height,
                                       uint32_t output_stride_pixels);

/* Convert only a contiguous output-row range.  The source coordinates remain
 * relative to the complete output image, which lets the Tank preview hold a
 * captured frame while each LCD DMA chunk is produced directly. */
bool rc_video_scale_vyuy_to_lcd_bgr565_region(const uint8_t *source,
                                             uint32_t source_width,
                                             uint32_t source_height,
                                             uint16_t *output,
                                             uint32_t output_width,
                                             uint32_t output_height,
                                             uint32_t output_y_start,
                                             uint32_t output_lines,
                                             uint32_t output_stride_pixels);
