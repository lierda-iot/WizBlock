#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMPANION_EXPRESSION_LOGICAL_WIDTH 80U
#define COMPANION_EXPRESSION_LOGICAL_HEIGHT 60U
#define COMPANION_EXPRESSION_PALETTE_LIMIT 16U
#define COMPANION_EXPRESSION_FRAME_BYTES 2400U
#define COMPANION_EXPRESSION_NO_SCENE_ID 0U

typedef struct {
    uint32_t scene_id;
    const uint8_t *pixels;
    size_t pixel_bytes;
} companion_expression_frame_t;

typedef struct {
    uint32_t pack_id;
    const char *pack_name;
    const char *display_name;
    const uint16_t *palette;
    size_t palette_count;
    const companion_expression_frame_t *frames;
    size_t frame_count;
} companion_expression_pack_t;

typedef struct {
    uint32_t scene_id;
    const char *scene_name;
    bool required;
    uint32_t fallback_scene_id;
} companion_expression_scene_t;

typedef struct {
    const companion_expression_pack_t *packs;
    size_t pack_count;
    const companion_expression_scene_t *scenes;
    size_t scene_count;
    uint32_t default_pack_id;
} companion_expression_catalog_t;

extern const companion_expression_catalog_t g_companion_expression_catalog;
