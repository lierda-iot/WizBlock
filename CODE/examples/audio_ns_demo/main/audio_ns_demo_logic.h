#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    AUDIO_NS_DEMO_STATE_PREPARING = 0,
    AUDIO_NS_DEMO_STATE_RECORDING,
    AUDIO_NS_DEMO_STATE_PLAYING_RAW,
    AUDIO_NS_DEMO_STATE_PLAYING_DENOISED,
    AUDIO_NS_DEMO_STATE_COMPLETE,
    AUDIO_NS_DEMO_STATE_ERROR,
} audio_ns_demo_state_t;

bool audio_ns_demo_restart_allowed(audio_ns_demo_state_t state);
uint8_t audio_ns_demo_progress_percent(size_t completed, size_t total);
