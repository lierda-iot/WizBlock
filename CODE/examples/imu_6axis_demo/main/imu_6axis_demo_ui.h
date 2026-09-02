#pragma once

#include "esp_err.h"
#include "imu_6axis_demo_logic.h"

#include <stdbool.h>
#include <stdint.h>

#define IMU_DEMO_ERROR_TEXT_LEN 48U

typedef struct {
    imu_demo_raw_vector_t accel_raw;
    imu_demo_raw_vector_t gyro_raw;
    imu_demo_sample_t sample;
    bool sensor_ok;
    bool calibrating;
    uint8_t calibration_percent;
    char error_text[IMU_DEMO_ERROR_TEXT_LEN];
} imu_demo_status_t;

typedef void (*imu_demo_calibrate_cb_t)(void *user_ctx);

typedef struct {
    imu_demo_calibrate_cb_t on_calibrate;
    void *user_ctx;
} imu_demo_ui_callbacks_t;

esp_err_t imu_demo_ui_init(const imu_demo_ui_callbacks_t *callbacks);
esp_err_t imu_demo_ui_update(const imu_demo_status_t *status);
