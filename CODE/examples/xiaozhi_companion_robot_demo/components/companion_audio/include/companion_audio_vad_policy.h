#pragma once

#include <stdbool.h>
#include <stdint.h>

#define COMPANION_AUDIO_VAD_MIN_SPEECH_MS 200U
#define COMPANION_AUDIO_VAD_TAIL_SILENCE_MS 1200U

typedef enum {
    COMPANION_AUDIO_VAD_ACTION_NONE = 0U,
    COMPANION_AUDIO_VAD_ACTION_START = 1U << 0,
    COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED = 1U << 1,
    COMPANION_AUDIO_VAD_ACTION_END = 1U << 2,
} companion_audio_vad_action_t;

typedef struct {
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t session_epoch;
    uint32_t request_id;
} companion_audio_vad_prompt_token_t;

typedef struct {
    uint32_t actions;
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t window_duration_ms;
    uint32_t accumulated_active_ms;
    uint32_t tail_silence_ms;
} companion_audio_vad_result_t;

typedef struct {
    uint32_t capture_generation;
    uint32_t capture_wake_seq;
    uint32_t capture_session_epoch;
    uint32_t capture_request_id;
    uint32_t window_generation;
    uint32_t window_wake_seq;
    uint64_t window_started_ms;
    uint64_t voice_window_started_ms;
    uint64_t tail_started_ms;
    uint64_t last_update_ms;
    uint32_t accumulated_active_ms;
    bool raw_active;
    bool voice_evidence_active;
    bool suppress_current_window;
    bool tail_active;
    bool speech_confirmed;
    bool has_last_update;
    bool accepting_speech;
    bool prompt_reset_pending;
    bool prompt_reset_in_progress;
    companion_audio_vad_prompt_token_t prompt_token;
} companion_audio_vad_tracker_t;

void companion_audio_vad_tracker_arm(
    companion_audio_vad_tracker_t *tracker, uint32_t generation,
    uint32_t wake_seq, bool vad_active);
void companion_audio_vad_tracker_arm_for_prompt(
    companion_audio_vad_tracker_t *tracker, uint32_t generation,
    uint32_t wake_seq, bool vad_active);
bool companion_audio_vad_tracker_retire(
    companion_audio_vad_tracker_t *tracker, uint32_t generation,
    uint32_t wake_seq, uint32_t session_epoch, uint32_t request_id);
bool companion_audio_vad_tracker_bind_upload_token(
    companion_audio_vad_tracker_t *tracker,
    const companion_audio_vad_prompt_token_t *token);
bool companion_audio_vad_tracker_prompt_started(
    companion_audio_vad_tracker_t *tracker,
    const companion_audio_vad_prompt_token_t *token);
bool companion_audio_vad_tracker_prompt_terminal(
    companion_audio_vad_tracker_t *tracker,
    const companion_audio_vad_prompt_token_t *token);
bool companion_audio_vad_tracker_take_prompt_reset(
    companion_audio_vad_tracker_t *tracker,
    companion_audio_vad_prompt_token_t *token);
bool companion_audio_vad_tracker_complete_prompt_reset(
    companion_audio_vad_tracker_t *tracker,
    const companion_audio_vad_prompt_token_t *token);
companion_audio_vad_result_t companion_audio_vad_tracker_step(
    companion_audio_vad_tracker_t *tracker, bool vad_active,
    uint64_t now_ms);
companion_audio_vad_result_t companion_audio_vad_tracker_step_with_voice(
    companion_audio_vad_tracker_t *tracker, bool vad_active,
    bool voice_evidence_active, uint64_t now_ms);
