#ifndef AUDIO_SPATIAL_SPECTRUM_UI_H
#define AUDIO_SPATIAL_SPECTRUM_UI_H

#include "audio_spatial_spectrum_doa_filter.h"
#include "audio_spatial_spectrum_math.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float combined_levels[AUDIO_SPECTRUM_BAND_COUNT];
    float combined_peaks[AUDIO_SPECTRUM_BAND_COUNT];
    float mic1_levels[AUDIO_SPECTRUM_BAND_COUNT];
    float mic1_peaks[AUDIO_SPECTRUM_BAND_COUNT];
    float mic2_levels[AUDIO_SPECTRUM_BAND_COUNT];
    float mic2_peaks[AUDIO_SPECTRUM_BAND_COUNT];
    float energy_db;
    float energy_dbfs;
    float relative_angle_deg;
    float mic1_level;
    float mic2_level;
    uint32_t mic1_rms;
    uint32_t mic2_rms;
    spatial_doa_direction_t direction;
    bool doa_active;
} audio_spatial_spectrum_ui_state_t;

esp_err_t audio_spatial_spectrum_ui_init(void);
esp_err_t audio_spatial_spectrum_ui_update(
    const audio_spatial_spectrum_ui_state_t *state);

#endif
