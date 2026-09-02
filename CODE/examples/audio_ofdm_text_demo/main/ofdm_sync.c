#include "ofdm_sync.h"

#include <math.h>

#define OFDM_SYNC_FINE_RADIUS OFDM_SYNC_COARSE_STEP
#define OFDM_SYNC_FINE_MIN_SCORE 0.20F
#define OFDM_SYNC_ENERGY_MIN 1.0e-12F
#define OFDM_SYNC_TRAINING_SCORE_EPSILON 1.0e-6F
#define OFDM_SYNC_TRAINING_REFINEMENT_MIN_GAIN 0.01F
#define OFDM_SYNC_TIMING_COARSE_STEP OFDM_SYNC_COARSE_STEP
#define OFDM_SYNC_TIMING_FINE_RADIUS (OFDM_SYNC_TIMING_COARSE_STEP - 1U)
#define OFDM_SYNC_PERIOD_LAG_RADIUS 8U

typedef struct {
    uint16_t lag;
    float score;
} ofdm_sync_period_match_t;

static float s_chirp_reference[OFDM_CHIRP_SAMPLES];
static float s_chirp_energy = 0.0F;
static float s_chirp_coarse_energy = 0.0F;
static bool s_initialized = false;

_Static_assert(0U == (OFDM_CHIRP_SAMPLES % OFDM_SYNC_COARSE_SAMPLE_STEP),
               "coarse chirp stride must divide the chirp length");
_Static_assert(0U == (OFDM_SYNC_TIMING_SEARCH_SAMPLES %
                      OFDM_SYNC_TIMING_COARSE_STEP),
               "timing search range must align to its coarse step");

static float calculate_chirp_score_squared(const float *samples,
                                           size_t offset,
                                           size_t sample_step,
                                           float reference_energy)
{
    float correlation = 0.0F;
    float sample_energy = 0.0F;
    for (size_t index = 0U; index < OFDM_CHIRP_SAMPLES;
         index += sample_step) {
        const float sample = samples[offset + index];
        if (!isfinite(sample)) {
            return 0.0F;
        }
        correlation += sample * s_chirp_reference[index];
        sample_energy += sample * sample;
    }
    const float denominator = sample_energy * reference_energy;
    return denominator > OFDM_SYNC_ENERGY_MIN
               ? (correlation * correlation) / denominator
               : 0.0F;
}

static float calculate_training_metric(const float *samples,
                                       float *sc_score,
                                       float *lts_score)
{
    if (!ofdm_phy_measure_training(samples, sc_score, lts_score)) {
        return 0.0F;
    }
    return fminf(*sc_score, *lts_score);
}

static float calculate_period_score(const float *samples,
                                    size_t lag,
                                    size_t compare_count)
{
    float correlation = 0.0F;
    float first_energy = 0.0F;
    float second_energy = 0.0F;
    for (size_t index = 0U; index < compare_count; ++index) {
        const float first = samples[index];
        const float second = samples[index + lag];
        correlation += first * second;
        first_energy += first * first;
        second_energy += second * second;
    }
    const float denominator = sqrtf(first_energy * second_energy);
    return denominator > OFDM_SYNC_ENERGY_MIN
               ? correlation / denominator
               : 0.0F;
}

static ofdm_sync_period_match_t find_period_lag(
    const float *samples,
    size_t sample_count,
    size_t expected_lag)
{
    ofdm_sync_period_match_t best = {0};
    const size_t first_lag = expected_lag - OFDM_SYNC_PERIOD_LAG_RADIUS;
    const size_t last_lag = expected_lag + OFDM_SYNC_PERIOD_LAG_RADIUS;
    const size_t compare_count = sample_count - last_lag;

    for (size_t lag = first_lag; lag <= last_lag; ++lag) {
        const float score = calculate_period_score(
            samples, lag, compare_count);
        if (fabsf(score) > fabsf(best.score)) {
            best.lag = (uint16_t)lag;
            best.score = score;
        }
    }
    return best;
}

static float calculate_rms(const float *samples, size_t sample_count)
{
    float energy = 0.0F;
    for (size_t index = 0U; index < sample_count; ++index) {
        energy += samples[index] * samples[index];
    }
    return sqrtf(energy / (float)sample_count);
}

static void measure_candidate_periods(const float *frame,
                                      ofdm_sync_match_t *match)
{
    const float *sc = &frame[OFDM_FRAME_SC_OFFSET];
    const float *lts = &frame[OFDM_FRAME_LTS_OFFSET];
    const size_t lts_sample_count =
        OFDM_LTS_SYMBOL_COUNT * OFDM_SYMBOL_SAMPLES;
    const ofdm_sync_period_match_t sc_period = find_period_lag(
        sc, OFDM_SYMBOL_SAMPLES, OFDM_FFT_SIZE / 2U);
    const ofdm_sync_period_match_t lts_short_period = find_period_lag(
        lts, lts_sample_count, OFDM_FFT_SIZE);
    const ofdm_sync_period_match_t lts_long_period = find_period_lag(
        lts, lts_sample_count, OFDM_SYMBOL_SAMPLES);

    match->sc_period_lag = sc_period.lag;
    match->sc_period_score = sc_period.score;
    match->lts_short_period_lag = lts_short_period.lag;
    match->lts_short_period_score = lts_short_period.score;
    match->lts_long_period_lag = lts_long_period.lag;
    match->lts_long_period_score = lts_long_period.score;
    match->sc_rms = calculate_rms(sc, OFDM_SYMBOL_SAMPLES);
    match->lts_rms = calculate_rms(lts, lts_sample_count);
}

static void consider_training_offset(const float *samples,
                                     size_t candidate_offset,
                                     size_t *best_offset,
                                     float *best_metric,
                                     float *best_sc_score,
                                     float *best_lts_score)
{
    float candidate_sc_score = 0.0F;
    float candidate_lts_score = 0.0F;
    const float candidate_metric = calculate_training_metric(
        &samples[candidate_offset], &candidate_sc_score,
        &candidate_lts_score);
    const bool stronger = candidate_metric >
                          *best_metric + OFDM_SYNC_TRAINING_SCORE_EPSILON;
    const bool later_equivalent =
        OFDM_SYNC_TRAINING_SCORE_EPSILON < candidate_metric &&
        fabsf(candidate_metric - *best_metric) <=
            OFDM_SYNC_TRAINING_SCORE_EPSILON &&
        *best_offset < candidate_offset;
    if (!stronger && !later_equivalent) {
        return;
    }

    *best_metric = candidate_metric;
    *best_offset = candidate_offset;
    *best_sc_score = candidate_sc_score;
    *best_lts_score = candidate_lts_score;
}

static size_t refine_frame_offset(const float *samples,
                                  size_t sample_count,
                                  size_t frame_offset,
                                  float *sc_score,
                                  float *lts_score)
{
    float best_sc_score = 0.0F;
    float best_lts_score = 0.0F;
    float best_metric = calculate_training_metric(
        &samples[frame_offset], &best_sc_score, &best_lts_score);
    const float initial_sc_score = best_sc_score;
    const float initial_lts_score = best_lts_score;
    const float initial_metric = best_metric;
    size_t best_offset = frame_offset;

    const size_t last_frame_offset =
        sample_count - OFDM_FRAME_SAMPLE_COUNT;
    const size_t backward_limit =
        frame_offset < OFDM_SYNC_TIMING_SEARCH_SAMPLES
            ? frame_offset
            : OFDM_SYNC_TIMING_SEARCH_SAMPLES;
    const size_t available_forward = last_frame_offset - frame_offset;
    const size_t forward_limit =
        available_forward < OFDM_SYNC_TIMING_SEARCH_SAMPLES
            ? available_forward
            : OFDM_SYNC_TIMING_SEARCH_SAMPLES;
    const size_t search_first = frame_offset - backward_limit;
    const size_t search_last = frame_offset + forward_limit;

    for (size_t distance = OFDM_SYNC_TIMING_COARSE_STEP;
         distance <= OFDM_SYNC_TIMING_SEARCH_SAMPLES;
         distance += OFDM_SYNC_TIMING_COARSE_STEP) {
        if (distance <= backward_limit) {
            consider_training_offset(
                samples, frame_offset - distance, &best_offset,
                &best_metric, &best_sc_score, &best_lts_score);
        }
        if (distance <= forward_limit) {
            consider_training_offset(
                samples, frame_offset + distance, &best_offset,
                &best_metric, &best_sc_score, &best_lts_score);
        }
    }

    const size_t fine_first =
        best_offset > search_first + OFDM_SYNC_TIMING_FINE_RADIUS
            ? best_offset - OFDM_SYNC_TIMING_FINE_RADIUS
            : search_first;
    const size_t fine_last =
        best_offset + OFDM_SYNC_TIMING_FINE_RADIUS < search_last
            ? best_offset + OFDM_SYNC_TIMING_FINE_RADIUS
            : search_last;
    for (size_t candidate_offset = fine_first;
         candidate_offset <= fine_last; ++candidate_offset) {
        consider_training_offset(
            samples, candidate_offset, &best_offset, &best_metric,
            &best_sc_score, &best_lts_score);
    }

    if (OFDM_PHY_SC_MIN_SCORE <= initial_sc_score &&
        OFDM_PHY_LTS_MIN_SCORE <= initial_lts_score &&
        OFDM_SYNC_TRAINING_SCORE_EPSILON <
            best_metric - initial_metric &&
        best_metric < initial_metric +
                          OFDM_SYNC_TRAINING_REFINEMENT_MIN_GAIN) {
        best_offset = frame_offset;
        best_sc_score = initial_sc_score;
        best_lts_score = initial_lts_score;
    }

    *sc_score = best_sc_score;
    *lts_score = best_lts_score;
    return best_offset;
}

ofdm_sync_result_t ofdm_sync_init(void)
{
    ofdm_phy_fill_chirp(s_chirp_reference);
    s_chirp_energy = 0.0F;
    s_chirp_coarse_energy = 0.0F;
    for (size_t index = 0U; index < OFDM_CHIRP_SAMPLES; ++index) {
        s_chirp_energy += s_chirp_reference[index] *
                          s_chirp_reference[index];
        if (0U == (index % OFDM_SYNC_COARSE_SAMPLE_STEP)) {
            s_chirp_coarse_energy += s_chirp_reference[index] *
                                     s_chirp_reference[index];
        }
    }
    s_initialized = s_chirp_energy > OFDM_SYNC_ENERGY_MIN &&
                    s_chirp_coarse_energy > OFDM_SYNC_ENERGY_MIN;
    return s_initialized ? OFDM_SYNC_OK : OFDM_SYNC_NOT_INITIALIZED;
}

void ofdm_sync_deinit(void)
{
    s_initialized = false;
    s_chirp_energy = 0.0F;
    s_chirp_coarse_energy = 0.0F;
}

ofdm_sync_result_t ofdm_sync_find_frame(
    const float *samples,
    size_t sample_count,
    ofdm_sync_match_t *match)
{
    return ofdm_sync_find_frame_from(samples, sample_count, 0U, match);
}

ofdm_sync_result_t ofdm_sync_find_frame_from(
    const float *samples,
    size_t sample_count,
    size_t first_frame_offset,
    ofdm_sync_match_t *match)
{
    if (NULL == match) {
        return OFDM_SYNC_INVALID_ARGUMENT;
    }
    *match = (ofdm_sync_match_t){0};
    if (NULL == samples) {
        return OFDM_SYNC_INVALID_ARGUMENT;
    }
    if (!s_initialized) {
        return OFDM_SYNC_NOT_INITIALIZED;
    }
    if (sample_count < OFDM_FRAME_SAMPLE_COUNT +
                           OFDM_SYNC_TIMING_SEARCH_SAMPLES) {
        return OFDM_SYNC_NOT_FOUND;
    }

    const size_t last_frame_offset =
        sample_count - OFDM_FRAME_SAMPLE_COUNT -
        OFDM_SYNC_TIMING_SEARCH_SAMPLES;
    if (last_frame_offset < first_frame_offset) {
        return OFDM_SYNC_NOT_FOUND;
    }
    float best_coarse_score_squared = 0.0F;
    size_t best_offset = first_frame_offset;
    for (size_t frame_offset = first_frame_offset;
         frame_offset <= last_frame_offset;
         frame_offset += OFDM_SYNC_COARSE_STEP) {
        const float score_squared = calculate_chirp_score_squared(
            samples, frame_offset + OFDM_FRAME_CHIRP_OFFSET,
            OFDM_SYNC_COARSE_SAMPLE_STEP, s_chirp_coarse_energy);
        if (score_squared > best_coarse_score_squared) {
            best_coarse_score_squared = score_squared;
            best_offset = frame_offset;
        }
    }
    match->best_chirp_score = sqrtf(best_coarse_score_squared);
    if ((OFDM_SYNC_FINE_MIN_SCORE * OFDM_SYNC_FINE_MIN_SCORE) >
        best_coarse_score_squared) {
        return OFDM_SYNC_NOT_FOUND;
    }

    const size_t fine_first = best_offset > OFDM_SYNC_FINE_RADIUS
                                  ? best_offset - OFDM_SYNC_FINE_RADIUS
                                  : 0U;
    const size_t fine_last = best_offset + OFDM_SYNC_FINE_RADIUS <
                                     last_frame_offset
                                 ? best_offset + OFDM_SYNC_FINE_RADIUS
                                 : last_frame_offset;
    float best_score_squared = 0.0F;
    for (size_t frame_offset = fine_first;
         frame_offset <= fine_last; ++frame_offset) {
        const float score_squared = calculate_chirp_score_squared(
            samples, frame_offset + OFDM_FRAME_CHIRP_OFFSET, 1U,
            s_chirp_energy);
        if (score_squared > best_score_squared) {
            best_score_squared = score_squared;
            best_offset = frame_offset;
        }
    }
    match->best_chirp_score = sqrtf(best_score_squared);

    if ((OFDM_SYNC_COMPOSITE_MIN_SCORE *
         OFDM_SYNC_COMPOSITE_MIN_SCORE) > best_score_squared) {
        return OFDM_SYNC_NOT_FOUND;
    }
    const size_t chirp_frame_offset = best_offset;
    float training_sc_score = 0.0F;
    float training_lts_score = 0.0F;
    best_offset = refine_frame_offset(
        samples, sample_count, chirp_frame_offset, &training_sc_score,
        &training_lts_score);
    const bool chirp_match =
        (OFDM_SYNC_MIN_SCORE * OFDM_SYNC_MIN_SCORE) <= best_score_squared;
    const bool training_match =
        OFDM_PHY_SC_MIN_SCORE <= training_sc_score &&
        OFDM_PHY_LTS_MIN_SCORE <= training_lts_score;
    match->frame_offset = best_offset;
    match->chirp_offset = chirp_frame_offset + OFDM_FRAME_CHIRP_OFFSET;
    match->timing_correction_samples =
        (int32_t)best_offset - (int32_t)chirp_frame_offset;
    match->training_sc_score = training_sc_score;
    match->training_lts_score = training_lts_score;
    measure_candidate_periods(&samples[best_offset], match);
    if (!chirp_match && !training_match) {
        return OFDM_SYNC_NOT_FOUND;
    }
    match->used_training_match = !chirp_match;
    match->chirp_score = match->best_chirp_score;
    return OFDM_SYNC_OK;
}
