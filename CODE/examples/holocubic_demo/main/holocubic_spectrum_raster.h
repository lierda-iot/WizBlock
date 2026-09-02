#ifndef HOLOCUBIC_SPECTRUM_RASTER_H
#define HOLOCUBIC_SPECTRUM_RASTER_H

#include "audio_spatial_spectrum_math.h"

#include <stdbool.h>
#include <stdint.h>

#define HOLO_SPECTRUM_CANVAS_WIDTH 240U
#define HOLO_SPECTRUM_CANVAS_HEIGHT 240U
#define HOLO_SPECTRUM_CANVAS_PIXELS \
    (HOLO_SPECTRUM_CANVAS_WIDTH * HOLO_SPECTRUM_CANVAS_HEIGHT)
#define HOLO_SPECTRUM_RASTER_MODE_COUNT 6U
#define HOLO_SPECTRUM_WATERFALL_ROWS 18U
#define HOLO_SPECTRUM_DOA_TRAIL_LENGTH 6U
#define HOLO_SPECTRUM_METABALL_COUNT 3U

typedef enum {
    HOLO_SPECTRUM_RADAR = 0,
    HOLO_SPECTRUM_MIRROR,
    HOLO_SPECTRUM_WATERFALL,
    HOLO_SPECTRUM_METABALLS,
    HOLO_SPECTRUM_LEVEL,
    HOLO_SPECTRUM_DUAL,
} holocubic_spectrum_mode_t;

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
    uint32_t revision;
    bool doa_active;
    bool available;
} holocubic_spectrum_snapshot_t;

typedef struct {
    float waterfall[HOLO_SPECTRUM_WATERFALL_ROWS]
                   [AUDIO_SPECTRUM_BAND_COUNT];
    float doa_trail[HOLO_SPECTRUM_DOA_TRAIL_LENGTH];
    float metaball_levels[HOLO_SPECTRUM_METABALL_COUNT];
    float metaball_doa_offset;
    float slow_level;
    uint32_t waterfall_revision;
    uint32_t doa_revision;
    uint8_t waterfall_head;
    uint8_t waterfall_count;
    uint8_t doa_trail_count;
} holocubic_spectrum_raster_state_t;

void holocubic_spectrum_raster_reset(holocubic_spectrum_raster_state_t *state);
void holocubic_spectrum_raster_draw(
    uint16_t *canvas,
    const holocubic_spectrum_snapshot_t *snapshot,
    holocubic_spectrum_mode_t mode,
    uint32_t now_ms,
    holocubic_spectrum_raster_state_t *state);

#endif
