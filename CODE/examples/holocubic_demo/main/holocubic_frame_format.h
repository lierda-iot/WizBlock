#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOLO_FRAME_WIDTH 240U
#define HOLO_FRAME_HEIGHT 240U
#define HOLO_FRAME_PIXELS (HOLO_FRAME_WIDTH * HOLO_FRAME_HEIGHT)
#define HOLO_FRAME_BYTES (HOLO_FRAME_PIXELS * sizeof(uint16_t))
#define HOLO_FRAME_PERIOD_MS 100U
#define HOLO_FRAME_IMAGE_MAGIC "HFRM"
#define HOLO_FRAME_IMAGE_MAGIC_BYTES 4U
#define HOLO_FRAME_IMAGE_VERSION 1U
#define HOLO_FRAME_IMAGE_HEADER_BYTES 32U
#define HOLO_FRAME_PIXEL_FORMAT_RGB565LE 1U
#define HOLO_FRAME_IMAGE_FRAME_COUNT 49U
#define HOLO_FRAME_IMAGE_PAYLOAD_BYTES \
    (HOLO_FRAME_IMAGE_FRAME_COUNT * HOLO_FRAME_BYTES)
#define HOLO_FRAME_IMAGE_BYTES \
    (HOLO_FRAME_IMAGE_HEADER_BYTES + HOLO_FRAME_IMAGE_PAYLOAD_BYTES)

typedef struct {
    uint16_t frame_count;
    uint32_t frame_period_ms;
    size_t payload_offset;
    size_t payload_bytes;
    uint32_t payload_crc32;
} holocubic_frame_image_info_t;

uint16_t holocubic_rgb565_to_display(uint16_t pixel);
bool holocubic_frame_cache_plan(uint16_t frame_count,
                                size_t cache_budget_bytes,
                                size_t *cache_bytes);
uint16_t holocubic_frame_next_index(uint16_t current_index,
                                    uint16_t frame_count);
uint32_t holocubic_frame_revision_next(uint32_t revision);
uint32_t holocubic_frame_crc32_begin(void);
uint32_t holocubic_frame_crc32_update(uint32_t state,
                                      const uint8_t *data,
                                      size_t data_bytes);
uint32_t holocubic_frame_crc32_finish(uint32_t state);
uint32_t holocubic_frame_crc32(const uint8_t *data, size_t data_bytes);
bool holocubic_frame_image_parse(const uint8_t *header,
                                 size_t header_bytes,
                                 size_t partition_bytes,
                                 holocubic_frame_image_info_t *info);
bool holocubic_frame_image_frame_offset(
    const holocubic_frame_image_info_t *info,
    uint16_t frame_index,
    size_t *frame_offset);

bool holocubic_frame_scale_to_canvas(const uint16_t *frame,
                                     size_t frame_pixels,
                                     uint16_t *canvas,
                                     size_t canvas_pixels);
