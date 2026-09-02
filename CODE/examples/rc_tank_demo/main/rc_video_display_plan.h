#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RC_TANK_LCD_PIXEL_CLOCK_HZ 20000000U
#define RC_REMOTE_LCD_PIXEL_CLOCK_HZ 40000000U
#define RC_VIDEO_LCD_INIT_DRAW_BUFFER_LINES 80U

/* Remote uses a rotated 320x240 logical surface backed by a portrait LCD bus. */
#define RC_REMOTE_LCD_LOGICAL_W 320U
#define RC_REMOTE_LCD_LOGICAL_H 240U
#define RC_REMOTE_LCD_INIT_W 240U
#define RC_REMOTE_LCD_INIT_DRAW_BUFFER_LINES RC_VIDEO_LCD_INIT_DRAW_BUFFER_LINES
#define RC_REMOTE_LCD_CHUNK_LINES 60U
#define RC_REMOTE_LCD_MAX_TRANSFER_BYTES \
    (RC_REMOTE_LCD_INIT_W * RC_REMOTE_LCD_INIT_DRAW_BUFFER_LINES * sizeof(uint16_t))

#define RC_TANK_STATUS_LCD_W 320U
#define RC_TANK_STATUS_LCD_H 240U
#define RC_TANK_STATUS_LCD_CHUNK_LINES RC_REMOTE_LCD_CHUNK_LINES

static inline uint32_t rc_remote_lcd_chunk_count(uint32_t height)
{
    return (height + RC_REMOTE_LCD_CHUNK_LINES - 1U) / RC_REMOTE_LCD_CHUNK_LINES;
}

static inline uint32_t rc_remote_lcd_chunk_lines(uint32_t height, uint32_t y)
{
    if (y >= height) {
        return 0U;
    }
    uint32_t remaining = height - y;
    return (remaining < RC_REMOTE_LCD_CHUNK_LINES) ? remaining : RC_REMOTE_LCD_CHUNK_LINES;
}

static inline bool rc_remote_lcd_chunk_fits_dma(uint32_t width, uint32_t lines)
{
    return (width * lines * sizeof(uint16_t)) <= RC_REMOTE_LCD_MAX_TRANSFER_BYTES;
}

static inline uint32_t rc_tank_status_lcd_chunk_count(void)
{
    return rc_remote_lcd_chunk_count(RC_TANK_STATUS_LCD_H);
}

static inline uint32_t rc_tank_status_lcd_chunk_lines(uint32_t y)
{
    return rc_remote_lcd_chunk_lines(RC_TANK_STATUS_LCD_H, y);
}
