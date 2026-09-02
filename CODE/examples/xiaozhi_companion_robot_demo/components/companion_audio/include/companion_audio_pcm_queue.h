#pragma once

#include "companion_audio.h"
#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define COMPANION_AUDIO_PCM_QUEUE_DEPTH 8U
#define COMPANION_AUDIO_PCM_FRAME_SAMPLES \
    (COMPANION_AUDIO_SAMPLE_RATE_HZ * COMPANION_AUDIO_OPUS_FRAME_MS / 1000U)

typedef struct {
    int16_t samples[COMPANION_AUDIO_PCM_FRAME_SAMPLES];
    companion_audio_token_t token;
} companion_audio_pcm_frame_t;

typedef struct {
    companion_audio_pcm_frame_t *storage;
    size_t capacity;
    size_t read_index;
    size_t write_index;
    size_t count;
    size_t high_water;
    uint32_t drops;
} companion_audio_pcm_queue_t;

esp_err_t companion_audio_pcm_queue_init(
    companion_audio_pcm_queue_t *queue,
    companion_audio_pcm_frame_t *storage, size_t capacity);
esp_err_t companion_audio_pcm_queue_push(
    companion_audio_pcm_queue_t *queue, const int16_t *samples,
    const companion_audio_token_t *token);
esp_err_t companion_audio_pcm_queue_pop(
    companion_audio_pcm_queue_t *queue,
    companion_audio_pcm_frame_t *frame);
void companion_audio_pcm_queue_clear(companion_audio_pcm_queue_t *queue);
size_t companion_audio_pcm_queue_count(
    const companion_audio_pcm_queue_t *queue);
