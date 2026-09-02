#include "companion_audio_signal_metrics.h"

#include <math.h>

#define MMR_CHANNELS 3U
#define MMR_MIC1 0U
#define MMR_MIC2 1U
#define MMR_REF 2U

typedef struct {
    double sum_x;
    double sum_y;
    double sum_x2;
    double sum_y2;
    double sum_xy;
    size_t count;
} correlation_t;

static int32_t sample_magnitude(int16_t sample)
{
    const int32_t value = sample;
    return (0 > value) ? -value : value;
}

static double correlation_value(const correlation_t *correlation)
{
    if (NULL == correlation || 2U > correlation->count) {
        return 0.0;
    }
    const double count = (double)correlation->count;
    const double numerator =
        count * correlation->sum_xy -
        correlation->sum_x * correlation->sum_y;
    const double energy_x =
        count * correlation->sum_x2 -
        correlation->sum_x * correlation->sum_x;
    const double energy_y =
        count * correlation->sum_y2 -
        correlation->sum_y * correlation->sum_y;
    if (0.0 >= energy_x || 0.0 >= energy_y) {
        return 0.0;
    }
    return numerator / (double)sqrtf((float)(energy_x * energy_y));
}

static void correlation_add(correlation_t *correlation,
                            int16_t x, int16_t y)
{
    const double x_value = (double)x;
    const double y_value = (double)y;
    correlation->sum_x += x_value;
    correlation->sum_y += y_value;
    correlation->sum_x2 += x_value * x_value;
    correlation->sum_y2 += y_value * y_value;
    correlation->sum_xy += x_value * y_value;
    correlation->count++;
}

bool companion_audio_signal_measure_mmr(
    const int16_t *mmr, size_t frames, size_t ref_lag_samples,
    companion_audio_signal_metrics_t *metrics)
{
    if (NULL == mmr || NULL == metrics || 0U == frames ||
        ref_lag_samples >= frames) {
        return false;
    }
    *metrics = (companion_audio_signal_metrics_t){
        .frames = frames,
    };
    correlation_t mic1_ref = {0};
    correlation_t mic2_ref = {0};
    for (size_t frame = 0U; frame < frames; ++frame) {
        const int16_t mic1 = mmr[frame * MMR_CHANNELS + MMR_MIC1];
        const int16_t mic2 = mmr[frame * MMR_CHANNELS + MMR_MIC2];
        const int16_t ref = mmr[frame * MMR_CHANNELS + MMR_REF];
        const int32_t mic1_peak = sample_magnitude(mic1);
        const int32_t mic2_peak = sample_magnitude(mic2);
        const int32_t ref_peak = sample_magnitude(ref);
        if (mic1_peak > metrics->mic1_peak) {
            metrics->mic1_peak = mic1_peak;
        }
        if (mic2_peak > metrics->mic2_peak) {
            metrics->mic2_peak = mic2_peak;
        }
        if (ref_peak > metrics->ref_peak) {
            metrics->ref_peak = ref_peak;
        }
        if (INT16_MAX == mic1 || INT16_MIN == mic1) {
            metrics->mic1_clipped_samples++;
        }
        if (INT16_MAX == mic2 || INT16_MIN == mic2) {
            metrics->mic2_clipped_samples++;
        }
        if (INT16_MAX == ref || INT16_MIN == ref) {
            metrics->ref_clipped_samples++;
        }
        if (frame >= ref_lag_samples) {
            const int16_t delayed_ref =
                mmr[(frame - ref_lag_samples) * MMR_CHANNELS + MMR_REF];
            correlation_add(&mic1_ref, mic1, delayed_ref);
            correlation_add(&mic2_ref, mic2, delayed_ref);
        }
    }
    metrics->mic1_ref_correlation = correlation_value(&mic1_ref);
    metrics->mic2_ref_correlation = correlation_value(&mic2_ref);
    return true;
}

uint32_t companion_audio_evaluate_realtime_window(
    const companion_audio_realtime_window_t *window)
{
    if (NULL == window || 0U == window->expected_samples ||
        0U == window->block_period_us) {
        return COMPANION_AUDIO_SIGNAL_INVALID_WINDOW;
    }
    uint32_t flags = 0U;
    if ((uint64_t)window->captured_samples * 10U <
        (uint64_t)window->expected_samples * 9U) {
        flags |= COMPANION_AUDIO_SIGNAL_CAPTURE_DEFICIT;
    }
    if (window->processor_max_us > window->block_period_us) {
        flags |= COMPANION_AUDIO_SIGNAL_PROCESSOR_OVERRUN;
    }
    if (0U != window->mic_clipped_samples) {
        flags |= COMPANION_AUDIO_SIGNAL_MIC_CLIPPING;
    }
    return flags;
}
