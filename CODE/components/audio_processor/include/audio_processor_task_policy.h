#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    uint8_t core;
    uint8_t priority;
} audio_processor_task_policy_t;

bool audio_processor_task_policy_is_valid(
    const audio_processor_task_policy_t *policy);
