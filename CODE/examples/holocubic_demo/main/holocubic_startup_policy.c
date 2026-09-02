#include "holocubic_startup_policy.h"

bool holocubic_startup_plan(holocubic_startup_step_t *steps,
                            size_t capacity)
{
    if (NULL == steps || capacity < HOLO_STARTUP_STEP_COUNT) {
        return false;
    }
    steps[0U] = HOLO_STARTUP_STEP_NETWORK;
    steps[1U] = HOLO_STARTUP_STEP_TOUCH;
    steps[2U] = HOLO_STARTUP_STEP_SPECTRUM;
    steps[3U] = HOLO_STARTUP_STEP_RENDER;
    steps[4U] = HOLO_STARTUP_STEP_FRAMES;
    return true;
}

holocubic_task_stack_memory_t holocubic_startup_task_stack(
    holocubic_startup_step_t step)
{
    return HOLO_STARTUP_STEP_SPECTRUM == step ||
                   HOLO_STARTUP_STEP_RENDER == step ?
               HOLO_TASK_STACK_EXTERNAL :
               HOLO_TASK_STACK_INTERNAL;
}

holocubic_task_stack_memory_t holocubic_runtime_task_stack(
    holocubic_runtime_task_t task)
{
    return HOLO_RUNTIME_TASK_NETWORK == task ? HOLO_TASK_STACK_INTERNAL :
                                               HOLO_TASK_STACK_EXTERNAL;
}
