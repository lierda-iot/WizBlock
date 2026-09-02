#include "audio_ns_demo_logic.h"

bool audio_ns_demo_restart_allowed(audio_ns_demo_state_t state)
{
    return (AUDIO_NS_DEMO_STATE_COMPLETE == state) ||
           (AUDIO_NS_DEMO_STATE_ERROR == state);
}

uint8_t audio_ns_demo_progress_percent(size_t completed, size_t total)
{
    if (0U == total) {
        return 0U;
    }
    if (completed >= total) {
        return 100U;
    }
    return (uint8_t)((completed * 100U) / total);
}
