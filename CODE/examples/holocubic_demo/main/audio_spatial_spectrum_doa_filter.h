#ifndef AUDIO_SPATIAL_SPECTRUM_DOA_FILTER_H
#define AUDIO_SPATIAL_SPECTRUM_DOA_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#define SPATIAL_DOA_FILTER_WINDOW_SIZE 5U
#define SPATIAL_DOA_ZERO_CONFIRMATION_FRAMES 3U

typedef enum {
    SPATIAL_DOA_DIRECTION_IDLE = 0,
    SPATIAL_DOA_DIRECTION_LEFT,
    SPATIAL_DOA_DIRECTION_CENTER,
    SPATIAL_DOA_DIRECTION_RIGHT,
} spatial_doa_direction_t;

typedef struct {
    float samples[SPATIAL_DOA_FILTER_WINDOW_SIZE];
    float filtered_deg;
    uint8_t sample_count;
    uint8_t next_sample;
    uint8_t zero_sample_count;
    bool initialized;
} spatial_doa_filter_t;

void spatial_doa_filter_reset(spatial_doa_filter_t *filter);
bool spatial_doa_filter_update(spatial_doa_filter_t *filter,
                               float raw_deg,
                               float *filtered_deg);
spatial_doa_direction_t spatial_doa_direction_update(spatial_doa_direction_t previous,
                                                     float filtered_deg);

#endif

