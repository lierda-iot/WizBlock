#pragma once

#include "dl_detect_base.hpp"

#include <cstddef>

typedef struct {
    size_t internal_limit_per_model_bytes;
    size_t internal_bytes;
    size_t psram_bytes;
} flash_param_face_model_memory_t;

dl::detect::Detect *create_flash_param_face_detector(float score_thr,
                                                      float nms_thr,
                                                      flash_param_face_model_memory_t *memory);
