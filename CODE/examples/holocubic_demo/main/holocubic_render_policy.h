#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "holocubic_model.h"

typedef struct {
    uint16_t chunk_lines;
    bool source_dma_required;
    bool horizontal_mirror;
    bool rotate_180;
} holocubic_render_policy_t;

typedef struct {
    uint32_t page_revision;
    uint32_t content_revision;
    bool valid;
} holocubic_present_gate_t;

holocubic_render_policy_t holocubic_render_policy_default(void);
uint32_t holocubic_content_revision(holocubic_page_t page,
                                    bool has_frame,
                                    uint32_t frame_revision,
                                    uint32_t weather_revision,
                                    bool has_spectrum,
                                    uint32_t spectrum_revision,
                                    uint8_t spectrum_mode,
                                    uint32_t now_ms,
                                    bool time_valid);
uint16_t holocubic_render_mirror_x(bool horizontal_mirror,
                                   uint16_t output_x,
                                   uint16_t output_width);
uint16_t holocubic_render_sample_x(bool horizontal_mirror,
                                   bool rotate_180,
                                   uint16_t output_x,
                                   uint16_t output_width);
uint16_t holocubic_render_sample_y(bool rotate_180,
                                   uint16_t output_y,
                                   uint16_t output_height);
bool holocubic_render_buffer_bytes(const holocubic_render_policy_t *policy,
                                   size_t width,
                                   size_t bytes_per_pixel,
                                   size_t *buffer_bytes);
bool holocubic_present_gate_should_present(
    const holocubic_present_gate_t *gate,
    uint32_t page_revision,
    uint32_t content_revision);
void holocubic_present_gate_mark_presented(holocubic_present_gate_t *gate,
                                           uint32_t page_revision,
                                           uint32_t content_revision);
void holocubic_present_gate_invalidate(holocubic_present_gate_t *gate);
