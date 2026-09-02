#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t frames;
    uint32_t mic1_clipped_samples;
    uint32_t mic2_clipped_samples;
    uint32_t ref_clipped_samples;
    int32_t mic1_peak;
    int32_t mic2_peak;
    int32_t ref_peak;
    double mic1_ref_correlation;
    double mic2_ref_correlation;
} companion_audio_signal_metrics_t;

typedef struct {
    uint32_t captured_samples;
    uint32_t expected_samples;
    uint32_t processor_max_us;
    uint32_t block_period_us;
    uint32_t mic_clipped_samples;
} companion_audio_realtime_window_t;

enum {
    COMPANION_AUDIO_SIGNAL_CAPTURE_DEFICIT = 1U << 0,
    COMPANION_AUDIO_SIGNAL_PROCESSOR_OVERRUN = 1U << 1,
    COMPANION_AUDIO_SIGNAL_MIC_CLIPPING = 1U << 2,
    COMPANION_AUDIO_SIGNAL_INVALID_WINDOW = 1U << 3,
};

bool companion_audio_signal_measure_mmr(
    const int16_t *mmr, size_t frames, size_t ref_lag_samples,
    companion_audio_signal_metrics_t *metrics);
uint32_t companion_audio_evaluate_realtime_window(
    const companion_audio_realtime_window_t *window);
