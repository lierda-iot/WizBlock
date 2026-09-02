#include "companion_audio_processor_policy.h"

audio_processor_config_t companion_audio_make_processor_config(void)
{
    return (audio_processor_config_t){
        .mic_channels = 2U,
        .ref_channels = 1U,
        .enable_ns = true,
        .enable_aec = true,
        .enable_vad = true,
        .enable_wakenet = true,
        .wakenet_threshold = COMPANION_AUDIO_WAKENET_THRESHOLD,
        .aec_mode = COMPANION_AUDIO_AEC_MODE_VOIP_LOW_COST,
        .afe_mode = AUDIO_PROCESSOR_AFE_MODE_LOW_COST,
        .afe_task_policy_valid = true,
        .afe_task_core = 1U,
        .afe_task_priority = 5U,
    };
}

companion_audio_task_policy_t companion_audio_make_task_policy(void)
{
    return (companion_audio_task_policy_t){
        .playback_core = COMPANION_AUDIO_PLAYBACK_TASK_CORE,
        .feed_core = COMPANION_AUDIO_FEED_TASK_CORE,
        .fetch_core = COMPANION_AUDIO_FETCH_TASK_CORE,
        .encode_core = COMPANION_AUDIO_ENCODE_TASK_CORE,
        .playback_priority = COMPANION_AUDIO_PLAYBACK_TASK_PRIORITY,
        .feed_priority = COMPANION_AUDIO_FEED_TASK_PRIORITY,
        .fetch_priority = COMPANION_AUDIO_FETCH_TASK_PRIORITY,
        .encode_priority = COMPANION_AUDIO_ENCODE_TASK_PRIORITY,
    };
}
