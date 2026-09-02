#pragma once

#include <stdint.h>

#include "audio_processor.h"

#define COMPANION_AUDIO_WAKENET_THRESHOLD 0.65f
#define COMPANION_AUDIO_AEC_MODE_VOIP_LOW_COST 3

#define COMPANION_AUDIO_PLAYBACK_TASK_CORE 1U
#define COMPANION_AUDIO_FEED_TASK_CORE 0U
#define COMPANION_AUDIO_FETCH_TASK_CORE 1U
#define COMPANION_AUDIO_ENCODE_TASK_CORE 1U
#define COMPANION_AUDIO_PLAYBACK_TASK_PRIORITY 4U
#define COMPANION_AUDIO_FEED_TASK_PRIORITY 5U
#define COMPANION_AUDIO_FETCH_TASK_PRIORITY 6U
#define COMPANION_AUDIO_ENCODE_TASK_PRIORITY 3U

typedef struct {
    uint8_t playback_core;
    uint8_t feed_core;
    uint8_t fetch_core;
    uint8_t encode_core;
    uint8_t playback_priority;
    uint8_t feed_priority;
    uint8_t fetch_priority;
    uint8_t encode_priority;
} companion_audio_task_policy_t;

audio_processor_config_t companion_audio_make_processor_config(void);
companion_audio_task_policy_t companion_audio_make_task_policy(void);
