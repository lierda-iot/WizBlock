#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "holocubic_model.h"
#include "holocubic_weather.h"

#define HOLO_VISUAL_CANVAS_WIDTH HOLO_LOGICAL_WIDTH
#define HOLO_VISUAL_CANVAS_HEIGHT HOLO_LOGICAL_HEIGHT
#define HOLO_VISUAL_CANVAS_PIXELS \
    (HOLO_VISUAL_CANVAS_WIDTH * HOLO_VISUAL_CANVAS_HEIGHT)
#define HOLO_VISUAL_ASSET_WIDTH 160U
#define HOLO_VISUAL_ASSET_HEIGHT 112U
#define HOLO_VISUAL_ASSET_PIXELS \
    (HOLO_VISUAL_ASSET_WIDTH * HOLO_VISUAL_ASSET_HEIGHT)
#define HOLO_VISUAL_ASSET_X 40U
#define HOLO_VISUAL_ASSET_Y 24U

typedef enum {
    HOLO_WEATHER_VISUAL_CLEAR = 0,
    HOLO_WEATHER_VISUAL_CLOUDY,
    HOLO_WEATHER_VISUAL_OVERCAST,
    HOLO_WEATHER_VISUAL_FOG,
    HOLO_WEATHER_VISUAL_RAIN,
    HOLO_WEATHER_VISUAL_SNOW,
    HOLO_WEATHER_VISUAL_STORM,
    HOLO_WEATHER_VISUAL_OFFLINE,
    HOLO_WEATHER_VISUAL_COUNT,
} holocubic_weather_visual_t;

typedef struct {
    const uint16_t *pixels;
    size_t pixel_count;
    uint16_t width;
    uint16_t height;
} holocubic_visual_bitmap_t;

holocubic_weather_visual_t holocubic_visual_weather_kind(
    int16_t weather_code);
bool holocubic_visual_draw_weather(uint16_t *canvas, size_t canvas_pixels,
                                   const holocubic_weather_t *weather,
                                   const holocubic_visual_bitmap_t *bitmap);
bool holocubic_visual_draw_clock(uint16_t *canvas, size_t canvas_pixels,
                                 const char *clock_text,
                                 const char *date_text,
                                 bool time_valid,
                                 const holocubic_visual_bitmap_t *bitmap);
