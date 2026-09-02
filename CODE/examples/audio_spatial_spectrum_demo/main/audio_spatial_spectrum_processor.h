#ifndef AUDIO_SPATIAL_SPECTRUM_PROCESSOR_H
#define AUDIO_SPATIAL_SPECTRUM_PROCESSOR_H

#include "audio_spatial_spectrum_math.h"

#include "esp_err.h"

#include <stdint.h>

#define AUDIO_SPATIAL_FFT_SIZE 512U

typedef struct {
    float combined_levels[AUDIO_SPECTRUM_BAND_COUNT];
    float combined_peaks[AUDIO_SPECTRUM_BAND_COUNT];
    float mic1_levels[AUDIO_SPECTRUM_BAND_COUNT];
    float mic1_peaks[AUDIO_SPECTRUM_BAND_COUNT];
    float mic2_levels[AUDIO_SPECTRUM_BAND_COUNT];
    float mic2_peaks[AUDIO_SPECTRUM_BAND_COUNT];
    float energy_db;
    float energy_dbfs;
    float mic1_level;
    float mic2_level;
    uint32_t mic1_rms;
    uint32_t mic2_rms;
} audio_spatial_spectrum_result_t;

esp_err_t audio_spatial_spectrum_processor_init(uint32_t sample_rate_hz);
esp_err_t audio_spatial_spectrum_processor_process(
    const int16_t *mic1_samples,
    const int16_t *mic2_samples,
    audio_spatial_spectrum_result_t *result);

#endif
