#include "holocubic_render_policy.h"

#include <stdint.h>

#define HOLO_RENDER_DMA_CHUNK_LINES 80U

holocubic_render_policy_t holocubic_render_policy_default(void)
{
    return (holocubic_render_policy_t){
        .chunk_lines = HOLO_RENDER_DMA_CHUNK_LINES,
        .source_dma_required = true,
        .horizontal_mirror = true,
        .rotate_180 = true,
    };
}

uint32_t holocubic_content_revision(holocubic_page_t page,
                                    bool has_frame,
                                    uint32_t frame_revision,
                                    uint32_t weather_revision,
                                    bool has_spectrum,
                                    uint32_t spectrum_revision,
                                    uint8_t spectrum_mode,
                                    uint32_t now_ms,
                                    bool time_valid)
{
    if (HOLO_PAGE_ANIMATION == page) {
        return has_frame ? frame_revision : now_ms / 100U;
    }
    if (HOLO_PAGE_CLOCK == page) {
        return ((now_ms / 1000U) << 1U) | (time_valid ? 1U : 0U);
    }
    if (HOLO_PAGE_SPECTRUM == page) {
        if (has_spectrum) {
            return (spectrum_revision << 3U) |
                   ((uint32_t)spectrum_mode % HOLO_SPECTRUM_MODE_COUNT);
        }
        return now_ms / 100U;
    }
    return weather_revision;
}

uint16_t holocubic_render_mirror_x(bool horizontal_mirror,
                                   uint16_t output_x,
                                   uint16_t output_width)
{
    if (0U == output_width || output_x >= output_width) {
        return 0U;
    }
    return horizontal_mirror ?
           (uint16_t)(output_width - 1U - output_x) : output_x;
}

uint16_t holocubic_render_sample_x(bool horizontal_mirror,
                                   bool rotate_180,
                                   uint16_t output_x,
                                   uint16_t output_width)
{
    if (0U == output_width || output_x >= output_width) {
        return 0U;
    }
    const uint16_t rotated_x = rotate_180 ?
        (uint16_t)(output_width - 1U - output_x) : output_x;
    return holocubic_render_mirror_x(horizontal_mirror, rotated_x,
                                     output_width);
}

uint16_t holocubic_render_sample_y(bool rotate_180,
                                   uint16_t output_y,
                                   uint16_t output_height)
{
    if (0U == output_height || output_y >= output_height) {
        return 0U;
    }
    return rotate_180 ?
           (uint16_t)(output_height - 1U - output_y) : output_y;
}

bool holocubic_render_buffer_bytes(const holocubic_render_policy_t *policy,
                                   size_t width,
                                   size_t bytes_per_pixel,
                                   size_t *buffer_bytes)
{
    if (NULL == policy || NULL == buffer_bytes || 0U == policy->chunk_lines ||
        0U == width || 0U == bytes_per_pixel) {
        return false;
    }
    if (width > (SIZE_MAX / bytes_per_pixel) / policy->chunk_lines) {
        return false;
    }
    *buffer_bytes = width * bytes_per_pixel * policy->chunk_lines;
    return true;
}

bool holocubic_present_gate_should_present(
    const holocubic_present_gate_t *gate,
    uint32_t page_revision,
    uint32_t content_revision)
{
    if (NULL == gate) return false;
    return !gate->valid || gate->page_revision != page_revision ||
           gate->content_revision != content_revision;
}

void holocubic_present_gate_mark_presented(holocubic_present_gate_t *gate,
                                           uint32_t page_revision,
                                           uint32_t content_revision)
{
    if (NULL == gate) return;
    gate->page_revision = page_revision;
    gate->content_revision = content_revision;
    gate->valid = true;
}

void holocubic_present_gate_invalidate(holocubic_present_gate_t *gate)
{
    if (NULL != gate) gate->valid = false;
}
