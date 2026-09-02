#include "holocubic_input.h"

#include <stddef.h>

#define HOLO_TOUCH_THRESHOLD_PX 30

void holocubic_touch_begin(holocubic_touch_gesture_t *gesture,
                           int16_t x, int16_t y)
{
    if (NULL == gesture) {
        return;
    }
    *gesture = (holocubic_touch_gesture_t){
        .start_x = x,
        .start_y = y,
        .active = true,
        .locked = false,
    };
}

holocubic_touch_event_t holocubic_touch_update(holocubic_touch_gesture_t *gesture,
                                               int16_t x, int16_t y)
{
    int16_t dx = 0;
    int16_t dy = 0;

    if (NULL == gesture || !gesture->active || gesture->locked) {
        return HOLO_TOUCH_NONE;
    }
    dx = (int16_t)(x - gesture->start_x);
    dy = (int16_t)(y - gesture->start_y);
    if (dx < 0) dx = (int16_t)-dx;
    if (dy < 0) dy = (int16_t)-dy;
    if (dx < HOLO_TOUCH_THRESHOLD_PX && dy < HOLO_TOUCH_THRESHOLD_PX) {
        return HOLO_TOUCH_NONE;
    }
    if (dx <= dy || dx < HOLO_TOUCH_THRESHOLD_PX) {
        return HOLO_TOUCH_NONE;
    }
    gesture->locked = true;
    return (x < gesture->start_x) ? HOLO_TOUCH_NEXT : HOLO_TOUCH_PREVIOUS;
}

holocubic_touch_event_t holocubic_touch_end(holocubic_touch_gesture_t *gesture)
{
    holocubic_touch_event_t event = HOLO_TOUCH_NONE;

    if (NULL == gesture || !gesture->active) {
        return HOLO_TOUCH_NONE;
    }
    if (!gesture->locked) {
        event = HOLO_TOUCH_CONFIRM;
    }
    gesture->active = false;
    gesture->locked = false;
    return event;
}

static int32_t absolute_value(int32_t value)
{
    return (0 > value) ? -value : value;
}

static bool imu_release_reached(const holocubic_imu_gesture_t *gesture,
                                int32_t relative_x, int32_t relative_y)
{
    if (NULL == gesture) {
        return false;
    }
    if (HOLO_IMU_NEXT == gesture->last_event) {
        return relative_y > -HOLO_IMU_NEUTRAL_RAW;
    }
    if (HOLO_IMU_PREVIOUS == gesture->last_event) {
        return relative_y < HOLO_IMU_NEUTRAL_RAW;
    }
    if (HOLO_IMU_CONFIRM == gesture->last_event) {
        return relative_x < HOLO_IMU_NEUTRAL_RAW;
    }
    return absolute_value(relative_x) < HOLO_IMU_NEUTRAL_RAW &&
           absolute_value(relative_y) < HOLO_IMU_NEUTRAL_RAW;
}

void holocubic_imu_init(holocubic_imu_gesture_t *gesture)
{
    if (NULL != gesture) {
        *gesture = (holocubic_imu_gesture_t){0};
    }
}

holocubic_imu_event_t holocubic_imu_update(holocubic_imu_gesture_t *gesture,
                                           int16_t accel_x,
                                           int16_t accel_y,
                                           uint32_t now_ms)
{
    int32_t relative_x = 0;
    int32_t relative_y = 0;
    holocubic_imu_event_t candidate = HOLO_IMU_NONE;

    if (NULL == gesture) {
        return HOLO_IMU_NONE;
    }
    if (!gesture->calibrated) {
        gesture->baseline_x_sum += accel_x;
        gesture->baseline_y_sum += accel_y;
        gesture->calibration_count++;
        if (gesture->calibration_count >= HOLO_IMU_CALIBRATION_SAMPLES) {
            gesture->baseline_x = (int16_t)(gesture->baseline_x_sum /
                                            (int32_t)HOLO_IMU_CALIBRATION_SAMPLES);
            gesture->baseline_y = (int16_t)(gesture->baseline_y_sum /
                                            (int32_t)HOLO_IMU_CALIBRATION_SAMPLES);
            gesture->calibrated = true;
            gesture->armed = true;
        }
        return HOLO_IMU_NONE;
    }

    relative_x = (int32_t)accel_x - gesture->baseline_x;
    relative_y = (int32_t)accel_y - gesture->baseline_y;
    if (absolute_value(relative_x) < HOLO_IMU_NEUTRAL_RAW &&
        absolute_value(relative_y) < HOLO_IMU_NEUTRAL_RAW) {
        gesture->armed = true;
        gesture->candidate = HOLO_IMU_NONE;
        gesture->candidate_samples = 0U;
        return HOLO_IMU_NONE;
    }

    if (!gesture->armed && imu_release_reached(gesture, relative_x,
                                               relative_y)) {
        gesture->armed = true;
        gesture->candidate = HOLO_IMU_NONE;
        gesture->candidate_samples = 0U;
    }
    if (!gesture->armed ||
        (0U != gesture->last_event_ms &&
         (uint32_t)(now_ms - gesture->last_event_ms) < HOLO_IMU_COOLDOWN_MS)) {
        return HOLO_IMU_NONE;
    }
    if (relative_y <= -HOLO_IMU_TRIGGER_RAW) {
        candidate = HOLO_IMU_NEXT;
    } else if (relative_y >= HOLO_IMU_TRIGGER_RAW) {
        candidate = HOLO_IMU_PREVIOUS;
    } else if (relative_x >= HOLO_IMU_TRIGGER_RAW) {
        candidate = HOLO_IMU_CONFIRM;
    }
    if (HOLO_IMU_NONE == candidate) {
        gesture->candidate = HOLO_IMU_NONE;
        gesture->candidate_samples = 0U;
        return HOLO_IMU_NONE;
    }
    if (candidate != gesture->candidate) {
        gesture->candidate = candidate;
        gesture->candidate_samples = 1U;
        return HOLO_IMU_NONE;
    }
    if (gesture->candidate_samples < UINT8_MAX) {
        gesture->candidate_samples++;
    }
    if (gesture->candidate_samples < HOLO_IMU_STABLE_SAMPLES) {
        return HOLO_IMU_NONE;
    }
    gesture->armed = false;
    gesture->last_event_ms = now_ms;
    gesture->last_event = candidate;
    gesture->candidate = HOLO_IMU_NONE;
    gesture->candidate_samples = 0U;
    return candidate;
}
