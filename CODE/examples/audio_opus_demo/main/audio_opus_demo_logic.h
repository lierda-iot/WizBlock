#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    AUDIO_OPUS_DEMO_STATE_PREPARING = 0,
    AUDIO_OPUS_DEMO_STATE_RECORDING,
    AUDIO_OPUS_DEMO_STATE_ENCODING,
    AUDIO_OPUS_DEMO_STATE_PLAYING,
    AUDIO_OPUS_DEMO_STATE_COMPLETE,
    AUDIO_OPUS_DEMO_STATE_ERROR,
} audio_opus_demo_state_t;

bool audio_opus_demo_restart_allowed(audio_opus_demo_state_t state);
uint8_t audio_opus_demo_progress_percent(size_t completed, size_t total);
uint32_t audio_opus_demo_compression_percent(uint32_t raw_bytes, uint32_t encoded_bytes);
