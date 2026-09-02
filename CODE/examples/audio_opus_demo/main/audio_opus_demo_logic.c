#include "audio_opus_demo_logic.h"

bool audio_opus_demo_restart_allowed(audio_opus_demo_state_t state)
{
    return (AUDIO_OPUS_DEMO_STATE_COMPLETE == state) ||
           (AUDIO_OPUS_DEMO_STATE_ERROR == state);
}

uint8_t audio_opus_demo_progress_percent(size_t completed, size_t total)
{
    if (0U == total) {
        return 0U;
    }
    if (completed >= total) {
        return 100U;
    }
    return (uint8_t)((completed * 100U) / total);
}

uint32_t audio_opus_demo_compression_percent(uint32_t raw_bytes, uint32_t encoded_bytes)
{
    if (0U == raw_bytes || encoded_bytes >= raw_bytes) {
        return 0U;
    }
    return (uint32_t)(100ULL * (uint64_t)(raw_bytes - encoded_bytes) / raw_bytes);
}
