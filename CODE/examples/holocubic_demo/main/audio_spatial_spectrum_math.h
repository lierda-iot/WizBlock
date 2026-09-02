#ifndef AUDIO_SPATIAL_SPECTRUM_MATH_H
#define AUDIO_SPATIAL_SPECTRUM_MATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_SPECTRUM_BAND_COUNT 24U

typedef struct {
    uint16_t first_bin;
    uint16_t last_bin;
} audio_spectrum_band_t;

typedef struct {
    float level[AUDIO_SPECTRUM_BAND_COUNT];
    float peak[AUDIO_SPECTRUM_BAND_COUNT];
} audio_spectrum_envelope_t;

bool audio_spectrum_build_log_bands(uint32_t sample_rate_hz,
                                    size_t fft_size,
                                    float min_frequency_hz,
                                    float max_frequency_hz,
                                    audio_spectrum_band_t *bands,
                                    size_t band_count);

float audio_spectrum_combine_magnitude(float mic1_magnitude,
                                       float mic2_magnitude);

float audio_spectrum_level_from_db(float value_db,
                                   float floor_db,
                                   float ceiling_db);

float audio_spectrum_average_band_levels(const float *levels,
                                         size_t band_count,
                                         size_t first_band,
                                         size_t requested_count);

float audio_spectrum_dbfs_from_rms_pair(uint32_t mic1_rms,
                                        uint32_t mic2_rms,
                                        uint32_t full_scale);

size_t audio_spectrum_mode_step(size_t current_mode,
                                int direction,
                                size_t mode_count);

void audio_spectrum_envelope_update(audio_spectrum_envelope_t *envelope,
                                    const float *target_levels,
                                    size_t band_count,
                                    float attack,
                                    float release,
                                    float peak_decay);

float audio_spectrum_doa_relative(float filtered_angle_deg, float display_gain);

#endif

