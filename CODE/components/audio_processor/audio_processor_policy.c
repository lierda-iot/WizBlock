#include "audio_processor.h"

audio_processor_afe_mode_t audio_processor_resolve_afe_mode(
    const audio_processor_config_t *config)
{
    if (NULL != config &&
        AUDIO_PROCESSOR_AFE_MODE_LOW_COST == config->afe_mode) {
        return AUDIO_PROCESSOR_AFE_MODE_LOW_COST;
    }
    return AUDIO_PROCESSOR_AFE_MODE_HIGH_PERF;
}
