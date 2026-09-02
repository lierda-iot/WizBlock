#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    HOLO_STARTUP_STEP_NETWORK = 0,
    HOLO_STARTUP_STEP_TOUCH,
    HOLO_STARTUP_STEP_RENDER,
    HOLO_STARTUP_STEP_FRAMES,
    HOLO_STARTUP_STEP_SPECTRUM,
} holocubic_startup_step_t;

typedef enum {
    HOLO_TASK_STACK_INTERNAL = 0,
    HOLO_TASK_STACK_EXTERNAL,
} holocubic_task_stack_memory_t;

typedef enum {
    HOLO_RUNTIME_TASK_FRAMES = 0,
    HOLO_RUNTIME_TASK_NETWORK,
    HOLO_RUNTIME_TASK_INPUT,
    HOLO_RUNTIME_TASK_TIME,
} holocubic_runtime_task_t;

#define HOLO_STARTUP_STEP_COUNT 5U

bool holocubic_startup_plan(holocubic_startup_step_t *steps,
                            size_t capacity);
holocubic_task_stack_memory_t holocubic_startup_task_stack(
    holocubic_startup_step_t step);
holocubic_task_stack_memory_t holocubic_runtime_task_stack(
    holocubic_runtime_task_t task);
