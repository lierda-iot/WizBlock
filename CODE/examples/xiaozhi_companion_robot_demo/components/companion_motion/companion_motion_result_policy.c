#include "companion_motion_result_policy.h"

companion_motion_result_class_t companion_motion_result_classify(
    companion_motion_failure_stage_t failure_stage, bool cancelled,
    esp_err_t error, esp_err_t stop_error)
{
    if (cancelled) {
        return COMPANION_MOTION_RESULT_CANCELLED;
    }
    if (ESP_OK != stop_error ||
        COMPANION_MOTION_FAILURE_OUTPUT_START == failure_stage ||
        COMPANION_MOTION_FAILURE_STOP == failure_stage) {
        return COMPANION_MOTION_RESULT_PERMANENT_OUTPUT_FAILURE;
    }
    if (ESP_OK == error && COMPANION_MOTION_FAILURE_NONE == failure_stage) {
        return COMPANION_MOTION_RESULT_SUCCESS;
    }
    if (COMPANION_MOTION_FAILURE_FEEDBACK_STALL == failure_stage) {
        return COMPANION_MOTION_RESULT_RETRYABLE_ACTUATION_STALL;
    }
    return COMPANION_MOTION_RESULT_RETRYABLE_SENSOR_FAILURE;
}
