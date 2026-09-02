#ifndef OFDM_SYNC_H
#define OFDM_SYNC_H

#include <stddef.h>

#include "ofdm_phy.h"

#define OFDM_SYNC_COARSE_STEP 4U
#define OFDM_SYNC_COARSE_SAMPLE_STEP 4U
#define OFDM_SYNC_MIN_SCORE 0.55F
#define OFDM_SYNC_COMPOSITE_MIN_SCORE 0.45F
#define OFDM_SYNC_TIMING_SEARCH_SAMPLES OFDM_FFT_SIZE

typedef enum {
    OFDM_SYNC_OK = 0,
    OFDM_SYNC_INVALID_ARGUMENT,
    OFDM_SYNC_NOT_INITIALIZED,
    OFDM_SYNC_NOT_FOUND,
} ofdm_sync_result_t;

typedef struct {
    size_t frame_offset;
    size_t chirp_offset;
    int32_t timing_correction_samples;
    bool used_training_match;
    float training_sc_score;
    float training_lts_score;
    float best_chirp_score;
    float chirp_score;
    uint16_t sc_period_lag;
    uint16_t lts_short_period_lag;
    uint16_t lts_long_period_lag;
    float sc_period_score;
    float lts_short_period_score;
    float lts_long_period_score;
    float sc_rms;
    float lts_rms;
} ofdm_sync_match_t;

ofdm_sync_result_t ofdm_sync_init(void);
void ofdm_sync_deinit(void);
ofdm_sync_result_t ofdm_sync_find_frame(
    const float *samples,
    size_t sample_count,
    ofdm_sync_match_t *match);
ofdm_sync_result_t ofdm_sync_find_frame_from(
    const float *samples,
    size_t sample_count,
    size_t first_frame_offset,
    ofdm_sync_match_t *match);

#endif
