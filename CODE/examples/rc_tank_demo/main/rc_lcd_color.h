#pragma once

#include <stdint.h>

/*
 * display_hal is configured for the panel's BGR element order and expects
 * big-endian RGB565 bytes in memory. Keep the conversion in one local
 * contract so static UI pixels and decoded video use the same wire format.
 */
static inline uint16_t rc_lcd_rgb565_be(uint8_t red5, uint8_t green6, uint8_t blue5)
{
    const uint16_t bgr565 = (uint16_t)(((uint16_t)(blue5 & 0x1FU) << 11) |
                                       ((uint16_t)(green6 & 0x3FU) << 5) |
                                       (uint16_t)(red5 & 0x1FU));
    return (uint16_t)((bgr565 >> 8) | (bgr565 << 8));
}

static inline uint16_t rc_lcd_swap_rgb565_be(uint16_t rgb565_be)
{
    const uint16_t rgb565 = (uint16_t)((rgb565_be >> 8) | (rgb565_be << 8));
    const uint16_t bgr565 = (uint16_t)(((rgb565 & 0x001FU) << 11) |
                                       (rgb565 & 0x07E0U) |
                                       ((rgb565 >> 11) & 0x001FU));
    return (uint16_t)((bgr565 >> 8) | (bgr565 << 8));
}
