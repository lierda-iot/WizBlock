#pragma once

#include "companion_motion.h"

companion_motion_result_class_t companion_motion_result_classify(
    companion_motion_failure_stage_t failure_stage, bool cancelled,
    esp_err_t error, esp_err_t stop_error);
