#include "audio_processor_task_policy.h"

#include <stddef.h>

#define AUDIO_PROCESSOR_MAX_CORE 1U
#define AUDIO_PROCESSOR_MIN_TASK_PRIORITY 1U

bool audio_processor_task_policy_is_valid(
    const audio_processor_task_policy_t *policy)
{
    if (NULL == policy || !policy->valid) {
        return true;
    }
    return policy->core <= AUDIO_PROCESSOR_MAX_CORE &&
           policy->priority >= AUDIO_PROCESSOR_MIN_TASK_PRIORITY;
}
