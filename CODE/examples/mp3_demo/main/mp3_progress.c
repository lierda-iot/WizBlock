#include "mp3_progress.h"

#include <stddef.h>

static int32_t clamp_slider_value(int32_t value)
{
    if (value < MP3_PROGRESS_SLIDER_MIN) {
        return MP3_PROGRESS_SLIDER_MIN;
    }
    if (value > MP3_PROGRESS_SLIDER_MAX) {
        return MP3_PROGRESS_SLIDER_MAX;
    }
    return value;
}

static uint64_t multiply_ratio(uint64_t numerator, uint32_t multiplier,
                               uint64_t denominator)
{
    uint64_t quotient = 0U;
    uint64_t remainder = 0U;
    uint32_t highest_bit = 1U;

    if (0U == denominator || numerator >= denominator) {
        return multiplier;
    }
    while (highest_bit <= multiplier / 2U) {
        highest_bit <<= 1U;
    }
    for (; highest_bit > 0U; highest_bit >>= 1U) {
        quotient <<= 1U;
        if (remainder >= denominator - remainder) {
            remainder -= denominator - remainder;
            ++quotient;
        } else {
            remainder += remainder;
        }
        if (0U != (multiplier & highest_bit)) {
            if (remainder >= denominator - numerator) {
                remainder -= denominator - numerator;
                ++quotient;
            } else {
                remainder += numerator;
            }
        }
    }
    return quotient;
}

void mp3_progress_init(mp3_progress_t *progress)
{
    if (NULL == progress) {
        return;
    }
    *progress = (mp3_progress_t){0};
}

void mp3_progress_set_snapshot(mp3_progress_t *progress, uint32_t generation,
                               uint64_t position_ms, uint64_t duration_ms,
                               bool paused)
{
    if (NULL == progress) {
        return;
    }
    if (generation != progress->generation) {
        progress->dragging = false;
        progress->release_submitted = false;
        progress->suppress_slider_event = false;
    }
    progress->generation = generation;
    progress->duration_ms = duration_ms;
    progress->position_ms =
        (duration_ms > 0U && position_ms > duration_ms) ? duration_ms
                                                       : position_ms;
    progress->paused = paused;
}

bool mp3_progress_begin_drag(mp3_progress_t *progress)
{
    if (NULL == progress || 0U == progress->duration_ms ||
        progress->dragging) {
        return false;
    }
    progress->dragging = true;
    progress->release_submitted = false;
    return true;
}

bool mp3_progress_preview(const mp3_progress_t *progress, int32_t slider_value,
                          uint64_t *preview_ms)
{
    if (NULL == progress || NULL == preview_ms || !progress->dragging ||
        0U == progress->duration_ms || progress->suppress_slider_event) {
        return false;
    }
    *preview_ms = mp3_progress_value_to_ms(slider_value,
                                           progress->duration_ms);
    return true;
}

bool mp3_progress_release(mp3_progress_t *progress, int32_t slider_value,
                          mp3_seek_request_t *request)
{
    if (NULL == progress || NULL == request || !progress->dragging ||
        progress->release_submitted || 0U == progress->duration_ms) {
        return false;
    }
    request->generation = progress->generation;
    request->target_ms = mp3_progress_value_to_ms(slider_value,
                                                  progress->duration_ms);
    request->keep_paused = progress->paused;
    progress->release_submitted = true;
    progress->dragging = false;
    return true;
}

void mp3_progress_cancel_drag(mp3_progress_t *progress)
{
    if (NULL == progress) {
        return;
    }
    progress->dragging = false;
    progress->release_submitted = false;
}

void mp3_progress_set_programmatic_update(mp3_progress_t *progress,
                                          bool active)
{
    if (NULL != progress) {
        progress->suppress_slider_event = active;
    }
}

bool mp3_progress_accept_value_event(const mp3_progress_t *progress)
{
    return NULL != progress && progress->dragging &&
           !progress->suppress_slider_event;
}

uint64_t mp3_progress_value_to_ms(int32_t slider_value,
                                  uint64_t duration_ms)
{
    uint64_t value = (uint64_t)clamp_slider_value(slider_value);
    uint64_t whole = duration_ms / MP3_PROGRESS_SLIDER_MAX;
    uint64_t remainder = duration_ms % MP3_PROGRESS_SLIDER_MAX;

    return (whole * value) +
           ((remainder * value) / MP3_PROGRESS_SLIDER_MAX);
}

int32_t mp3_progress_ms_to_value(uint64_t position_ms,
                                 uint64_t duration_ms)
{
    if (0U == duration_ms || 0U == position_ms) {
        return MP3_PROGRESS_SLIDER_MIN;
    }
    if (position_ms >= duration_ms) {
        return MP3_PROGRESS_SLIDER_MAX;
    }
    return (int32_t)multiply_ratio(position_ms, MP3_PROGRESS_SLIDER_MAX,
                                   duration_ms);
}
