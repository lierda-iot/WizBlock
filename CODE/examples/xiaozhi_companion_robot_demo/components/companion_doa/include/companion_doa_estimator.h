#ifndef COMPANION_DOA_ESTIMATOR_H
#define COMPANION_DOA_ESTIMATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMPANION_DOA_ESTIMATOR_WINDOW 12U

typedef enum {
    COMPANION_DOA_DIRECTION_LEFT = 0,
    COMPANION_DOA_DIRECTION_CENTER,
    COMPANION_DOA_DIRECTION_RIGHT,
    COMPANION_DOA_DIRECTION_COUNT,
} companion_doa_direction_t;

typedef struct {
    bool valid;
    companion_doa_direction_t direction;
    float angle_deg;
    float raw_relative_deg;
    float relative_deg;
    float mad_deg;
    uint8_t sample_count;
    uint8_t winning_votes;
} companion_doa_estimate_t;

bool companion_doa_estimate(const float *samples, size_t count,
                            companion_doa_estimate_t *estimate);

#ifdef __cplusplus
}
#endif

#endif
