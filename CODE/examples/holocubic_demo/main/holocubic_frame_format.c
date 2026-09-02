#include "holocubic_frame_format.h"

#include "holocubic_model.h"

#include <string.h>

#define HOLO_CRC32_POLYNOMIAL 0xEDB88320U

static uint16_t read_le16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0U] |
                      ((uint16_t)source[1U] << 8U));
}

static uint32_t read_le32(const uint8_t *source)
{
    return (uint32_t)source[0U] |
           ((uint32_t)source[1U] << 8U) |
           ((uint32_t)source[2U] << 16U) |
           ((uint32_t)source[3U] << 24U);
}

uint16_t holocubic_rgb565_to_display(uint16_t pixel)
{
    const uint16_t red = (pixel >> 11U) & 0x1FU;
    const uint16_t green = (pixel >> 5U) & 0x3FU;
    const uint16_t blue = pixel & 0x1FU;
    const uint16_t bgr = (uint16_t)((blue << 11U) | (green << 5U) | red);
    return (uint16_t)((bgr >> 8U) | (bgr << 8U));
}

bool holocubic_frame_cache_plan(uint16_t frame_count,
                                size_t cache_budget_bytes,
                                size_t *cache_bytes)
{
    size_t required_bytes = 0U;

    if (NULL == cache_bytes) return false;
    *cache_bytes = 0U;
    if (0U == frame_count) return false;

    required_bytes = (size_t)frame_count * HOLO_FRAME_BYTES;
    if (required_bytes > cache_budget_bytes) return false;
    *cache_bytes = required_bytes;
    return true;
}

uint16_t holocubic_frame_next_index(uint16_t current_index,
                                    uint16_t frame_count)
{
    if (0U == frame_count || current_index >= frame_count) return 0U;
    return (uint16_t)((current_index + 1U) % frame_count);
}

uint32_t holocubic_frame_revision_next(uint32_t revision)
{
    return UINT32_MAX == revision ? 1U : revision + 1U;
}

uint32_t holocubic_frame_crc32_begin(void)
{
    return UINT32_MAX;
}

uint32_t holocubic_frame_crc32_update(uint32_t state,
                                      const uint8_t *data,
                                      size_t data_bytes)
{
    if (NULL == data && 0U < data_bytes) return state;
    for (size_t byte_index = 0U; byte_index < data_bytes; ++byte_index) {
        state ^= data[byte_index];
        for (uint8_t bit_index = 0U; bit_index < 8U; ++bit_index) {
            const uint32_t mask = 0U - (state & 1U);
            state = (state >> 1U) ^ (HOLO_CRC32_POLYNOMIAL & mask);
        }
    }
    return state;
}

uint32_t holocubic_frame_crc32_finish(uint32_t state)
{
    return state ^ UINT32_MAX;
}

uint32_t holocubic_frame_crc32(const uint8_t *data, size_t data_bytes)
{
    if (NULL == data && 0U < data_bytes) return 0U;
    return holocubic_frame_crc32_finish(holocubic_frame_crc32_update(
        holocubic_frame_crc32_begin(), data, data_bytes));
}

bool holocubic_frame_image_parse(const uint8_t *header,
                                 size_t header_bytes,
                                 size_t partition_bytes,
                                 holocubic_frame_image_info_t *info)
{
    uint16_t frame_count = 0U;
    uint32_t payload_bytes = 0U;

    if (NULL == header || NULL == info ||
        HOLO_FRAME_IMAGE_HEADER_BYTES > header_bytes) {
        return false;
    }
    *info = (holocubic_frame_image_info_t){0};
    frame_count = read_le16(header + 14U);
    payload_bytes = read_le32(header + 20U);
    if (0 != memcmp(header, HOLO_FRAME_IMAGE_MAGIC,
                    HOLO_FRAME_IMAGE_MAGIC_BYTES) ||
        HOLO_FRAME_IMAGE_VERSION != read_le16(header + 4U) ||
        HOLO_FRAME_IMAGE_HEADER_BYTES != read_le16(header + 6U) ||
        HOLO_FRAME_WIDTH != read_le16(header + 8U) ||
        HOLO_FRAME_HEIGHT != read_le16(header + 10U) ||
        HOLO_FRAME_PIXEL_FORMAT_RGB565LE != read_le16(header + 12U) ||
        HOLO_FRAME_IMAGE_FRAME_COUNT != frame_count ||
        HOLO_FRAME_PERIOD_MS != read_le32(header + 16U) ||
        HOLO_FRAME_IMAGE_PAYLOAD_BYTES != payload_bytes ||
        0U != read_le32(header + 28U) ||
        HOLO_FRAME_IMAGE_BYTES > partition_bytes) {
        return false;
    }
    info->frame_count = frame_count;
    info->frame_period_ms = read_le32(header + 16U);
    info->payload_offset = HOLO_FRAME_IMAGE_HEADER_BYTES;
    info->payload_bytes = payload_bytes;
    info->payload_crc32 = read_le32(header + 24U);
    return true;
}

bool holocubic_frame_image_frame_offset(
    const holocubic_frame_image_info_t *info,
    uint16_t frame_index,
    size_t *frame_offset)
{
    size_t offset = 0U;

    if (NULL == info || NULL == frame_offset ||
        frame_index >= info->frame_count ||
        HOLO_FRAME_IMAGE_FRAME_COUNT != info->frame_count ||
        HOLO_FRAME_IMAGE_PAYLOAD_BYTES != info->payload_bytes) {
        return false;
    }
    offset = info->payload_offset + ((size_t)frame_index * HOLO_FRAME_BYTES);
    if (offset < info->payload_offset ||
        offset + HOLO_FRAME_BYTES < offset ||
        offset + HOLO_FRAME_BYTES >
            info->payload_offset + info->payload_bytes) {
        return false;
    }
    *frame_offset = offset;
    return true;
}

bool holocubic_frame_scale_to_canvas(const uint16_t *frame,
                                     size_t frame_pixels,
                                     uint16_t *canvas,
                                     size_t canvas_pixels)
{
    if (NULL == frame || NULL == canvas ||
        HOLO_FRAME_PIXELS != frame_pixels ||
        (HOLO_LOGICAL_WIDTH * HOLO_LOGICAL_HEIGHT) != canvas_pixels) {
        return false;
    }

    if (HOLO_FRAME_WIDTH == HOLO_LOGICAL_WIDTH &&
        HOLO_FRAME_HEIGHT == HOLO_LOGICAL_HEIGHT) {
        memcpy(canvas, frame, HOLO_FRAME_BYTES);
        return true;
    }

    for (uint16_t output_y = 0U; output_y < HOLO_LOGICAL_HEIGHT; ++output_y) {
        const uint16_t source_y = holocubic_scale_coordinate(
            output_y, HOLO_LOGICAL_HEIGHT, HOLO_FRAME_HEIGHT);
        for (uint16_t output_x = 0U; output_x < HOLO_LOGICAL_WIDTH; ++output_x) {
            const uint16_t source_x = holocubic_scale_coordinate(
                output_x, HOLO_LOGICAL_WIDTH, HOLO_FRAME_WIDTH);
            canvas[(size_t)output_y * HOLO_LOGICAL_WIDTH + output_x] =
                frame[(size_t)source_y * HOLO_FRAME_WIDTH + source_x];
        }
    }
    return true;
}
