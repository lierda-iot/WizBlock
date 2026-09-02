#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "holocubic_frame_format.h"

typedef struct {
    uint16_t *cache;
    uint16_t cache_index;
    uint16_t frame_count;
    uint16_t next_frame;
    uint32_t revision;
    bool frame_ready;
    bool resource_ready;
} holocubic_frames_t;

typedef struct {
    const uint16_t *pixels;
    uint32_t revision;
} holocubic_frame_snapshot_t;

esp_err_t holocubic_frames_prepare(holocubic_frames_t *frames);
esp_err_t holocubic_frames_load(holocubic_frames_t *frames);
esp_err_t holocubic_frames_init(holocubic_frames_t *frames);
void holocubic_frames_deinit(holocubic_frames_t *frames);
void holocubic_frames_task(void *argument);
const uint16_t *holocubic_frames_current(const holocubic_frames_t *frames);
bool holocubic_frames_snapshot(const holocubic_frames_t *frames,
                               holocubic_frame_snapshot_t *snapshot);
