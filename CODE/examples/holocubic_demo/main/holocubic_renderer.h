#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "holocubic_model.h"
#include "holocubic_render_policy.h"
#include "holocubic_spectrum.h"
#include "holocubic_weather.h"

#define HOLO_RENDER_PHYSICAL_WIDTH 320U
#define HOLO_RENDER_PHYSICAL_HEIGHT 240U
#define HOLO_RENDER_FRAME_PIXELS (HOLO_LOGICAL_WIDTH * HOLO_LOGICAL_HEIGHT)

typedef struct {
    uint16_t *canvas;
    uint16_t *scale_buffer;
    holocubic_rect_t content_rect;
    uint16_t chunk_lines;
    bool horizontal_mirror;
    bool rotate_180;
    uint32_t frame_index;
    bool ready;
} holocubic_renderer_t;

esp_err_t holocubic_renderer_init(holocubic_renderer_t *renderer);
void holocubic_renderer_deinit(holocubic_renderer_t *renderer);
void holocubic_renderer_draw(holocubic_renderer_t *renderer,
                             holocubic_page_t page,
                             const holocubic_weather_t *weather,
                             const char *clock_text,
                             const char *date_text,
                             bool time_valid,
                             const uint16_t *animation_frame,
                             const holocubic_spectrum_snapshot_t *spectrum,
                             holocubic_spectrum_mode_t spectrum_mode,
                             uint32_t now_ms,
                             uint32_t frame_index);
esp_err_t holocubic_renderer_present(holocubic_renderer_t *renderer);
esp_err_t holocubic_renderer_clear_background(void);
