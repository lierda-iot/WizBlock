#include "audio_spatial_spectrum_math.h"

#include <math.h>

#define AUDIO_SPECTRUM_LEVEL_MIN 0.0f
#define AUDIO_SPECTRUM_LEVEL_MAX 1.0f
#define AUDIO_SPECTRUM_DOA_CENTER_DEG 90.0f
#define AUDIO_SPECTRUM_DOA_LIMIT_DEG 90.0f
#define AUDIO_SPECTRUM_DBFS_FLOOR -120.0f

static float clamp_unit(float value)
{
    if (!isfinite(value) || AUDIO_SPECTRUM_LEVEL_MIN > value) {
        return AUDIO_SPECTRUM_LEVEL_MIN;
    }
    if (AUDIO_SPECTRUM_LEVEL_MAX < value) {
        return AUDIO_SPECTRUM_LEVEL_MAX;
    }
    return value;
}

bool audio_spectrum_build_log_bands(uint32_t sample_rate_hz,
                                    size_t fft_size,
                                    float min_frequency_hz,
                                    float max_frequency_hz,
                                    audio_spectrum_band_t *bands,
                                    size_t band_count)
{
    if (NULL == bands || 0U == sample_rate_hz || 4U > fft_size ||
        0U == band_count || !isfinite(min_frequency_hz) ||
        !isfinite(max_frequency_hz) || 0.0f >= min_frequency_hz ||
        min_frequency_hz >= max_frequency_hz) {
        return false;
    }

    const float bin_width_hz = (float)sample_rate_hz / (float)fft_size;
    const size_t highest_spectrum_bin = (fft_size / 2U) - 1U;
    size_t first_bin = (size_t)ceilf(min_frequency_hz / bin_width_hz);
    size_t last_bin = (size_t)floorf(max_frequency_hz / bin_width_hz);

    if (1U > first_bin) {
        first_bin = 1U;
    }
    if (highest_spectrum_bin < last_bin) {
        last_bin = highest_spectrum_bin;
    }
    if (last_bin < first_bin || band_count > (last_bin - first_bin + 1U)) {
        return false;
    }

    const size_t final_edge = last_bin + 1U;
    const float ratio = (float)final_edge / (float)first_bin;
    size_t current_edge = first_bin;

    for (size_t index = 0; index < band_count; index++) {
        const size_t remaining_bands = band_count - index - 1U;
        size_t next_edge = final_edge;

        if (0U < remaining_bands) {
            const float exponent = (float)(index + 1U) / (float)band_count;
            next_edge = (size_t)lroundf((float)first_bin * powf(ratio, exponent));

            const size_t minimum_edge = current_edge + 1U;
            const size_t maximum_edge = final_edge - remaining_bands;
            if (minimum_edge > next_edge) {
                next_edge = minimum_edge;
            }
            if (maximum_edge < next_edge) {
                next_edge = maximum_edge;
            }
        }

        bands[index].first_bin = (uint16_t)current_edge;
        bands[index].last_bin = (uint16_t)(next_edge - 1U);
        current_edge = next_edge;
    }

    return true;
}

float audio_spectrum_combine_magnitude(float mic1_magnitude,
                                       float mic2_magnitude)
{
    const float mic1 = isfinite(mic1_magnitude) && 0.0f < mic1_magnitude ?
                       mic1_magnitude : 0.0f;
    const float mic2 = isfinite(mic2_magnitude) && 0.0f < mic2_magnitude ?
                       mic2_magnitude : 0.0f;
    return (mic1 + mic2) * 0.5f;
}

float audio_spectrum_level_from_db(float value_db,
                                   float floor_db,
                                   float ceiling_db)
{
    if (!isfinite(value_db) || !isfinite(floor_db) ||
        !isfinite(ceiling_db) || floor_db >= ceiling_db) {
        return 0.0f;
    }
    return clamp_unit((value_db - floor_db) / (ceiling_db - floor_db));
}

float audio_spectrum_average_band_levels(const float *levels,
                                         size_t band_count,
                                         size_t first_band,
                                         size_t requested_count)
{
    if (NULL == levels || 0U == band_count || first_band >= band_count ||
        0U == requested_count) {
        return 0.0f;
    }

    const size_t available_count = band_count - first_band;
    const size_t count = requested_count < available_count ?
                         requested_count : available_count;
    double sum = 0.0;
    for (size_t index = 0U; index < count; index++) {
        sum += clamp_unit(levels[first_band + index]);
    }
    return (float)(sum / (double)count);
}

float audio_spectrum_dbfs_from_rms_pair(uint32_t mic1_rms,
                                        uint32_t mic2_rms,
                                        uint32_t full_scale)
{
    if (0U == full_scale || (0U == mic1_rms && 0U == mic2_rms)) {
        return AUDIO_SPECTRUM_DBFS_FLOOR;
    }

    const double mean_square =
        ((double)mic1_rms * (double)mic1_rms +
         (double)mic2_rms * (double)mic2_rms) * 0.5;
    const double combined_rms = sqrt(mean_square);
    float level_dbfs = (float)(20.0 * log10(combined_rms / (double)full_scale));
    if (!isfinite(level_dbfs) || AUDIO_SPECTRUM_DBFS_FLOOR > level_dbfs) {
        level_dbfs = AUDIO_SPECTRUM_DBFS_FLOOR;
    } else if (0.0f < level_dbfs) {
        level_dbfs = 0.0f;
    }
    return level_dbfs;
}

size_t audio_spectrum_mode_step(size_t current_mode,
                                int direction,
                                size_t mode_count)
{
    if (0U == mode_count || current_mode >= mode_count) {
        return 0U;
    }
    if (0 > direction) {
        return 0U == current_mode ? mode_count - 1U : current_mode - 1U;
    }
    if (0 < direction) {
        return current_mode + 1U == mode_count ? 0U : current_mode + 1U;
    }
    return current_mode;
}

void audio_spectrum_envelope_update(audio_spectrum_envelope_t *envelope,
                                    const float *target_levels,
                                    size_t band_count,
                                    float attack,
                                    float release,
                                    float peak_decay)
{
    if (NULL == envelope || NULL == target_levels) {
        return;
    }

    if (AUDIO_SPECTRUM_BAND_COUNT < band_count) {
        band_count = AUDIO_SPECTRUM_BAND_COUNT;
    }
    attack = clamp_unit(attack);
    release = clamp_unit(release);
    peak_decay = clamp_unit(peak_decay);

    for (size_t index = 0; index < band_count; index++) {
        const float target = clamp_unit(target_levels[index]);
        const float coefficient = target > envelope->level[index] ? attack : release;
        float level = envelope->level[index] +
                      coefficient * (target - envelope->level[index]);
        level = clamp_unit(level);

        float decayed_peak = envelope->peak[index] - peak_decay;
        if (AUDIO_SPECTRUM_LEVEL_MIN > decayed_peak) {
            decayed_peak = AUDIO_SPECTRUM_LEVEL_MIN;
        }

        envelope->level[index] = level;
        envelope->peak[index] = level > decayed_peak ? level : decayed_peak;
    }
}

float audio_spectrum_doa_relative(float filtered_angle_deg, float display_gain)
{
    if (!isfinite(filtered_angle_deg) || !isfinite(display_gain) ||
        0.0f > display_gain) {
        return 0.0f;
    }

    float relative_deg = (AUDIO_SPECTRUM_DOA_CENTER_DEG - filtered_angle_deg) *
                         display_gain;
    if (-AUDIO_SPECTRUM_DOA_LIMIT_DEG > relative_deg) {
        relative_deg = -AUDIO_SPECTRUM_DOA_LIMIT_DEG;
    }
    if (AUDIO_SPECTRUM_DOA_LIMIT_DEG < relative_deg) {
        relative_deg = AUDIO_SPECTRUM_DOA_LIMIT_DEG;
    }
    return relative_deg;
}
