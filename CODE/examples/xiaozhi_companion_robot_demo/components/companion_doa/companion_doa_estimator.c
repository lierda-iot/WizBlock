#include "companion_doa_estimator.h"

#include <math.h>
#include <string.h>

#define COMPANION_DOA_MIN_VALID_SAMPLES 4U
#define COMPANION_DOA_MIN_WIN_PERCENT 55U
#define COMPANION_DOA_MIN_VOTE_LEAD 1U
#define COMPANION_DOA_LEFT_LIMIT_DEG 75.0f
#define COMPANION_DOA_RIGHT_LIMIT_DEG 105.0f
#define COMPANION_DOA_CENTER_DEG 90.0f
#define COMPANION_DOA_RELATIVE_GAIN 2.25f
#define COMPANION_DOA_MAX_MAD_DEG 20.0f
#define COMPANION_DOA_MAX_RELATIVE_DEG 90.0f
#define COMPANION_DOA_ZERO_MAX_DEG 0.5f
#define COMPANION_DOA_MAX_ANGLE_DEG 180.0f

static float median(float *values, size_t count)
{
    for (size_t index = 1U; index < count; ++index) {
        const float value = values[index];
        size_t position = index;
        while (0U < position && value < values[position - 1U]) {
            values[position] = values[position - 1U];
            position--;
        }
        values[position] = value;
    }
    if (0U == (count & 1U)) {
        return (values[count / 2U - 1U] + values[count / 2U]) * 0.5f;
    }
    return values[count / 2U];
}

static companion_doa_direction_t classify(float angle_deg)
{
    if (angle_deg < COMPANION_DOA_LEFT_LIMIT_DEG) {
        return COMPANION_DOA_DIRECTION_LEFT;
    }
    if (angle_deg > COMPANION_DOA_RIGHT_LIMIT_DEG) {
        return COMPANION_DOA_DIRECTION_RIGHT;
    }
    return COMPANION_DOA_DIRECTION_CENTER;
}

bool companion_doa_estimate(const float *samples, size_t count,
                            companion_doa_estimate_t *estimate)
{
    if (NULL == samples || NULL == estimate) {
        return false;
    }
    memset(estimate, 0, sizeof(*estimate));
    float recent[COMPANION_DOA_ESTIMATOR_WINDOW] = {0};
    size_t valid_count = 0U;
    for (size_t remaining = count;
         0U < remaining && valid_count < COMPANION_DOA_ESTIMATOR_WINDOW;
         --remaining) {
        const float angle = samples[remaining - 1U];
        if (isfinite(angle) && angle > COMPANION_DOA_ZERO_MAX_DEG &&
            angle <= COMPANION_DOA_MAX_ANGLE_DEG) {
            recent[valid_count++] = angle;
        }
    }
    estimate->sample_count = (uint8_t)valid_count;
    if (valid_count < COMPANION_DOA_MIN_VALID_SAMPLES) {
        return false;
    }

    uint8_t votes[COMPANION_DOA_DIRECTION_COUNT] = {0};
    for (size_t index = 0U; index < valid_count; ++index) {
        votes[classify(recent[index])]++;
    }
    companion_doa_direction_t winner = COMPANION_DOA_DIRECTION_LEFT;
    uint8_t runner_up = 0U;
    for (int direction = 1; direction < COMPANION_DOA_DIRECTION_COUNT;
         ++direction) {
        if (votes[direction] > votes[winner]) {
            runner_up = votes[winner];
            winner = (companion_doa_direction_t)direction;
        } else if (votes[direction] > runner_up) {
            runner_up = votes[direction];
        }
    }
    const uint8_t winning_votes = votes[winner];
    estimate->direction = winner;
    estimate->winning_votes = winning_votes;
    if ((uint32_t)winning_votes * 100U <
            (uint32_t)valid_count * COMPANION_DOA_MIN_WIN_PERCENT ||
        winning_votes < (uint8_t)(runner_up + COMPANION_DOA_MIN_VOTE_LEAD)) {
        return false;
    }

    float winning[COMPANION_DOA_ESTIMATOR_WINDOW] = {0};
    size_t winning_count = 0U;
    for (size_t index = 0U; index < valid_count; ++index) {
        if (winner == classify(recent[index])) {
            winning[winning_count++] = recent[index];
        }
    }
    const float angle = median(winning, winning_count);
    float deviations[COMPANION_DOA_ESTIMATOR_WINDOW] = {0};
    for (size_t index = 0U; index < winning_count; ++index) {
        deviations[index] = fabsf(winning[index] - angle);
    }
    const float mad = median(deviations, winning_count);
    estimate->angle_deg = angle;
    estimate->mad_deg = mad;
    if (mad > COMPANION_DOA_MAX_MAD_DEG) {
        return false;
    }

    const float raw_relative = COMPANION_DOA_CENTER_DEG - angle;
    float relative = raw_relative * COMPANION_DOA_RELATIVE_GAIN;
    if (relative > COMPANION_DOA_MAX_RELATIVE_DEG) {
        relative = COMPANION_DOA_MAX_RELATIVE_DEG;
    } else if (relative < -COMPANION_DOA_MAX_RELATIVE_DEG) {
        relative = -COMPANION_DOA_MAX_RELATIVE_DEG;
    }
    estimate->raw_relative_deg = raw_relative;
    estimate->relative_deg = relative;
    estimate->valid = true;
    return true;
}
