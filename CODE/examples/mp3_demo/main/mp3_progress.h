#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MP3_PROGRESS_SLIDER_MIN 0
#define MP3_PROGRESS_SLIDER_MAX 1000

typedef struct {
    uint32_t generation;
    uint64_t target_ms;
    bool keep_paused;
} mp3_seek_request_t;

typedef struct {
    uint32_t generation;
    uint64_t position_ms;
    uint64_t duration_ms;
    bool paused;
    bool dragging;
    bool suppress_slider_event;
    bool release_submitted;
} mp3_progress_t;

void mp3_progress_init(mp3_progress_t *progress);

void mp3_progress_set_snapshot(mp3_progress_t *progress, uint32_t generation,
                               uint64_t position_ms, uint64_t duration_ms,
                               bool paused);

bool mp3_progress_begin_drag(mp3_progress_t *progress);

bool mp3_progress_preview(const mp3_progress_t *progress, int32_t slider_value,
                          uint64_t *preview_ms);

bool mp3_progress_release(mp3_progress_t *progress, int32_t slider_value,
                          mp3_seek_request_t *request);

void mp3_progress_cancel_drag(mp3_progress_t *progress);

void mp3_progress_set_programmatic_update(mp3_progress_t *progress,
                                          bool active);

bool mp3_progress_accept_value_event(const mp3_progress_t *progress);

uint64_t mp3_progress_value_to_ms(int32_t slider_value,
                                  uint64_t duration_ms);

int32_t mp3_progress_ms_to_value(uint64_t position_ms,
                                 uint64_t duration_ms);
