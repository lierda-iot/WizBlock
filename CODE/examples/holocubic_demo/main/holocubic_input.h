#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HOLO_TOUCH_NONE = 0,
    HOLO_TOUCH_PREVIOUS,
    HOLO_TOUCH_NEXT,
    HOLO_TOUCH_CONFIRM,
} holocubic_touch_event_t;

typedef struct {
    int16_t start_x;
    int16_t start_y;
    bool active;
    bool locked;
} holocubic_touch_gesture_t;

void holocubic_touch_begin(holocubic_touch_gesture_t *gesture,
                           int16_t x, int16_t y);
holocubic_touch_event_t holocubic_touch_update(holocubic_touch_gesture_t *gesture,
                                               int16_t x, int16_t y);
holocubic_touch_event_t holocubic_touch_end(holocubic_touch_gesture_t *gesture);

#define HOLO_IMU_CALIBRATION_SAMPLES 20U
#define HOLO_IMU_TRIGGER_RAW 5000
#define HOLO_IMU_NEUTRAL_RAW 2500
#define HOLO_IMU_STABLE_SAMPLES 3U
#define HOLO_IMU_COOLDOWN_MS 500U

typedef enum {
    HOLO_IMU_NONE = 0,
    HOLO_IMU_PREVIOUS,
    HOLO_IMU_NEXT,
    HOLO_IMU_CONFIRM,
} holocubic_imu_event_t;

typedef struct {
    int32_t baseline_x_sum;
    int32_t baseline_y_sum;
    int16_t baseline_x;
    int16_t baseline_y;
    uint32_t calibration_count;
    uint32_t last_event_ms;
    holocubic_imu_event_t last_event;
    holocubic_imu_event_t candidate;
    uint8_t candidate_samples;
    bool calibrated;
    bool armed;
} holocubic_imu_gesture_t;

void holocubic_imu_init(holocubic_imu_gesture_t *gesture);
holocubic_imu_event_t holocubic_imu_update(holocubic_imu_gesture_t *gesture,
                                           int16_t accel_x,
                                           int16_t accel_y,
                                           uint32_t now_ms);
