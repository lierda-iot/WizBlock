#include "audio_dual_mic_doa_filter.h"

#include <stddef.h>
#include <string.h>

#define DOA_ANGLE_MIN_DEG              0.0f
#define DOA_ANGLE_MAX_DEG              180.0f
#define DOA_ANGLE_EMA_ALPHA            0.30f
#define DOA_ZERO_ANGLE_MAX_DEG         0.5f
#define DOA_RIGHT_ENTER_MAX_DEG        75.0f
#define DOA_RIGHT_EXIT_MIN_DEG         80.0f
#define DOA_LEFT_EXIT_MAX_DEG          100.0f
#define DOA_LEFT_ENTER_MIN_DEG         105.0f

static float clamp_angle(float angle_deg)
{
    if (DOA_ANGLE_MIN_DEG > angle_deg) {
        return DOA_ANGLE_MIN_DEG;
    }
    if (DOA_ANGLE_MAX_DEG < angle_deg) {
        return DOA_ANGLE_MAX_DEG;
    }
    return angle_deg;
}

static float calculate_median(const doa_angle_filter_t *filter)
{
    float sorted[DOA_ANGLE_FILTER_WINDOW_SIZE] = {0};
    uint8_t count = filter->sample_count;

    memcpy(sorted, filter->samples, (size_t)count * sizeof(sorted[0]));
    for (uint8_t index = 1U; index < count; index++) {
        float value = sorted[index];
        uint8_t position = index;
        while (0U < position && value < sorted[position - 1U]) {
            sorted[position] = sorted[position - 1U];
            position--;
        }
        sorted[position] = value;
    }

    if (0U == (count & 1U)) {
        uint8_t upper = count / 2U;
        return (sorted[upper - 1U] + sorted[upper]) * 0.5f;
    }
    return sorted[count / 2U];
}

void doa_angle_filter_reset(doa_angle_filter_t *filter)
{
    if (NULL == filter) {
        return;
    }
    memset(filter, 0, sizeof(*filter));
}

bool doa_angle_filter_update(doa_angle_filter_t *filter, float raw_deg,
                             float *filtered_deg)
{
    if (NULL == filter || NULL == filtered_deg) {
        return false;
    }

    float angle_deg = clamp_angle(raw_deg);
    if (DOA_ZERO_ANGLE_MAX_DEG >= angle_deg) {
        if (DOA_ZERO_CONFIRMATION_FRAMES > filter->zero_sample_count) {
            filter->zero_sample_count++;
        }
        if (DOA_ZERO_CONFIRMATION_FRAMES > filter->zero_sample_count) {
            if (filter->initialized) {
                *filtered_deg = filter->filtered_deg;
            }
            return false;
        }
    } else {
        filter->zero_sample_count = 0U;
    }

    filter->samples[filter->next_sample] = angle_deg;
    filter->next_sample = (uint8_t)((filter->next_sample + 1U) %
                                    DOA_ANGLE_FILTER_WINDOW_SIZE);
    if (DOA_ANGLE_FILTER_WINDOW_SIZE > filter->sample_count) {
        filter->sample_count++;
    }

    float median_deg = calculate_median(filter);
    if (!filter->initialized) {
        filter->filtered_deg = median_deg;
        filter->initialized = true;
    } else {
        filter->filtered_deg += DOA_ANGLE_EMA_ALPHA *
                                (median_deg - filter->filtered_deg);
    }
    *filtered_deg = filter->filtered_deg;
    return true;
}

doa_filter_direction_t doa_direction_filter_update(doa_filter_direction_t previous,
                                                   float filtered_deg)
{
    float angle_deg = clamp_angle(filtered_deg);

    if (DOA_FILTER_DIRECTION_RIGHT == previous) {
        if (DOA_RIGHT_EXIT_MIN_DEG > angle_deg) {
            return DOA_FILTER_DIRECTION_RIGHT;
        }
        if (DOA_LEFT_ENTER_MIN_DEG < angle_deg) {
            return DOA_FILTER_DIRECTION_LEFT;
        }
        return DOA_FILTER_DIRECTION_CENTER;
    }

    if (DOA_FILTER_DIRECTION_LEFT == previous) {
        if (DOA_LEFT_EXIT_MAX_DEG < angle_deg) {
            return DOA_FILTER_DIRECTION_LEFT;
        }
        if (DOA_RIGHT_ENTER_MAX_DEG > angle_deg) {
            return DOA_FILTER_DIRECTION_RIGHT;
        }
        return DOA_FILTER_DIRECTION_CENTER;
    }

    if (DOA_RIGHT_ENTER_MAX_DEG > angle_deg) {
        return DOA_FILTER_DIRECTION_RIGHT;
    }
    if (DOA_LEFT_ENTER_MIN_DEG < angle_deg) {
        return DOA_FILTER_DIRECTION_LEFT;
    }
    return DOA_FILTER_DIRECTION_CENTER;
}
