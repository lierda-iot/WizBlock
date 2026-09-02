#ifndef AUDIO_DUAL_MIC_DOA_FILTER_H
#define AUDIO_DUAL_MIC_DOA_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#define DOA_ANGLE_FILTER_WINDOW_SIZE 5U
#define DOA_ZERO_CONFIRMATION_FRAMES 3U

typedef enum {
    DOA_FILTER_DIRECTION_IDLE = 0,
    DOA_FILTER_DIRECTION_LEFT,
    DOA_FILTER_DIRECTION_CENTER,
    DOA_FILTER_DIRECTION_RIGHT,
} doa_filter_direction_t;

typedef struct {
    float samples[DOA_ANGLE_FILTER_WINDOW_SIZE];
    float filtered_deg;
    uint8_t sample_count;
    uint8_t next_sample;
    uint8_t zero_sample_count;
    bool initialized;
} doa_angle_filter_t;

void doa_angle_filter_reset(doa_angle_filter_t *filter);
bool doa_angle_filter_update(doa_angle_filter_t *filter, float raw_deg,
                             float *filtered_deg);
doa_filter_direction_t doa_direction_filter_update(doa_filter_direction_t previous,
                                                   float filtered_deg);

#endif
