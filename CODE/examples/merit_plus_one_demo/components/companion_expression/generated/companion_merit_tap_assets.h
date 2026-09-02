#pragma once

#include <stdint.h>

#define COMPANION_MERIT_BUBBLE_FRAME_COUNT 7U
#define COMPANION_MERIT_BUBBLE_TILE_ORIGIN_X 140U
#define COMPANION_MERIT_BUBBLE_TILE_ORIGIN_Y 0U
#define COMPANION_MERIT_BUBBLE_TILE_WIDTH 180U
#define COMPANION_MERIT_BUBBLE_TILE_HEIGHT 110U

typedef struct {
    const uint16_t *pixels;
    const uint8_t *alpha;
} companion_merit_bubble_asset_t;

extern const companion_merit_bubble_asset_t
    g_companion_merit_bubble_assets[COMPANION_MERIT_BUBBLE_FRAME_COUNT];
