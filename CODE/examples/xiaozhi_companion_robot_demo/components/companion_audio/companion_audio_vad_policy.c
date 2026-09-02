#include "companion_audio_vad_policy.h"

#include <limits.h>
#include <stddef.h>

static uint32_t elapsed_ms(uint64_t start_ms, uint64_t end_ms)
{
    if (end_ms < start_ms) {
        return 0U;
    }
    const uint64_t elapsed = end_ms - start_ms;
    return (UINT32_MAX < elapsed) ? UINT32_MAX : (uint32_t)elapsed;
}
static uint32_t saturating_add(uint32_t first, uint32_t second)
{
    return (UINT32_MAX - first < second) ? UINT32_MAX : first + second;
}

static bool capture_is_valid(const companion_audio_vad_tracker_t *tracker)
{
    return NULL != tracker && 0U != tracker->capture_generation &&
           0U != tracker->capture_wake_seq;
}

static bool window_is_current(const companion_audio_vad_tracker_t *tracker)
{
    return capture_is_valid(tracker) &&
           tracker->capture_generation == tracker->window_generation &&
           tracker->capture_wake_seq == tracker->window_wake_seq;
}

static bool prompt_token_is_valid(
    const companion_audio_vad_prompt_token_t *token)
{
    return NULL != token && 0U != token->generation &&
           0U != token->wake_seq && 0U != token->request_id;
}

static bool prompt_tokens_match(
    const companion_audio_vad_prompt_token_t *first,
    const companion_audio_vad_prompt_token_t *second)
{
    return prompt_token_is_valid(first) && prompt_token_is_valid(second) &&
           first->generation == second->generation &&
           first->wake_seq == second->wake_seq &&
           first->session_epoch == second->session_epoch &&
           first->request_id == second->request_id;
}

static bool capture_token_matches(
    const companion_audio_vad_tracker_t *tracker,
    uint32_t generation,
    uint32_t wake_seq,
    uint32_t session_epoch,
    uint32_t request_id)
{
    if (!capture_is_valid(tracker) || 0U == request_id ||
        tracker->capture_generation != generation ||
        tracker->capture_wake_seq != wake_seq ||
        tracker->capture_request_id != request_id) {
        return false;
    }
    return 0U == tracker->capture_session_epoch ||
           tracker->capture_session_epoch == session_epoch;
}

static void reset_utterance(companion_audio_vad_tracker_t *tracker)
{
    tracker->voice_window_started_ms = 0ULL;
    tracker->tail_started_ms = 0ULL;
    tracker->accumulated_active_ms = 0U;
    tracker->voice_evidence_active = false;
    tracker->tail_active = false;
    tracker->speech_confirmed = false;
}

static void reset_prompt_evidence(companion_audio_vad_tracker_t *tracker)
{
    reset_utterance(tracker);
    tracker->window_generation = 0U;
    tracker->window_wake_seq = 0U;
    tracker->window_started_ms = 0ULL;
    tracker->raw_active = false;
    tracker->suppress_current_window = false;
}

void companion_audio_vad_tracker_arm(
    companion_audio_vad_tracker_t *tracker, uint32_t generation,
    uint32_t wake_seq, bool vad_active)
{
    if (NULL == tracker) {
        return;
    }
    tracker->capture_generation = generation;
    tracker->capture_wake_seq = wake_seq;
    tracker->capture_session_epoch = 0U;
    tracker->capture_request_id = 0U;
    reset_utterance(tracker);
    tracker->suppress_current_window = vad_active && !tracker->raw_active;
    tracker->accepting_speech = true;
    tracker->prompt_reset_pending = false;
    tracker->prompt_reset_in_progress = false;
    tracker->prompt_token = (companion_audio_vad_prompt_token_t){0};
}

void companion_audio_vad_tracker_arm_for_prompt(
    companion_audio_vad_tracker_t *tracker, uint32_t generation,
    uint32_t wake_seq, bool vad_active)
{
    companion_audio_vad_tracker_arm(
        tracker, generation, wake_seq, vad_active);
    if (NULL != tracker) {
        tracker->accepting_speech = false;
    }
}

bool companion_audio_vad_tracker_prompt_started(
    companion_audio_vad_tracker_t *tracker,
    const companion_audio_vad_prompt_token_t *token)
{
    if (NULL == tracker || !prompt_token_is_valid(token) ||
        tracker->capture_generation != token->generation ||
        tracker->capture_wake_seq != token->wake_seq) {
        return false;
    }
    reset_prompt_evidence(tracker);
    tracker->capture_session_epoch = token->session_epoch;
    tracker->capture_request_id = token->request_id;
    tracker->accepting_speech = false;
    tracker->prompt_reset_pending = false;
    tracker->prompt_reset_in_progress = false;
    tracker->prompt_token = *token;
    return true;
}

bool companion_audio_vad_tracker_prompt_terminal(
    companion_audio_vad_tracker_t *tracker,
    const companion_audio_vad_prompt_token_t *token)
{
    if (NULL == tracker ||
        !prompt_tokens_match(token, &tracker->prompt_token) ||
        tracker->accepting_speech) {
        return false;
    }
    if (!tracker->prompt_reset_in_progress) {
        tracker->prompt_reset_pending = true;
    }
    return true;
}

bool companion_audio_vad_tracker_take_prompt_reset(
    companion_audio_vad_tracker_t *tracker,
    companion_audio_vad_prompt_token_t *token)
{
    if (NULL == tracker || NULL == token ||
        !tracker->prompt_reset_pending ||
        tracker->prompt_reset_in_progress) {
        return false;
    }
    *token = tracker->prompt_token;
    tracker->prompt_reset_pending = false;
    tracker->prompt_reset_in_progress = true;
    return true;
}

bool companion_audio_vad_tracker_complete_prompt_reset(
    companion_audio_vad_tracker_t *tracker,
    const companion_audio_vad_prompt_token_t *token)
{
    if (NULL == tracker || !tracker->prompt_reset_in_progress ||
        !prompt_tokens_match(token, &tracker->prompt_token) ||
        tracker->capture_generation != token->generation ||
        tracker->capture_wake_seq != token->wake_seq) {
        return false;
    }
    reset_prompt_evidence(tracker);
    tracker->accepting_speech = true;
    tracker->prompt_reset_in_progress = false;
    tracker->prompt_token = (companion_audio_vad_prompt_token_t){0};
    return true;
}

bool companion_audio_vad_tracker_bind_upload_token(
    companion_audio_vad_tracker_t *tracker,
    const companion_audio_vad_prompt_token_t *token)
{
    if (NULL == tracker || !prompt_token_is_valid(token) ||
        !capture_token_matches(tracker, token->generation,
                               token->wake_seq, token->session_epoch,
                               token->request_id)) {
        return false;
    }
    if (0U != tracker->capture_session_epoch &&
        tracker->capture_session_epoch != token->session_epoch) {
        return false;
    }
    tracker->capture_session_epoch = token->session_epoch;
    return true;
}

bool companion_audio_vad_tracker_retire(
    companion_audio_vad_tracker_t *tracker,
    uint32_t generation,
    uint32_t wake_seq,
    uint32_t session_epoch,
    uint32_t request_id)
{
    if (!capture_token_matches(tracker, generation, wake_seq,
                               session_epoch, request_id)) {
        return false;
    }
    *tracker = (companion_audio_vad_tracker_t){0};
    return true;
}

companion_audio_vad_result_t companion_audio_vad_tracker_step_with_voice(
    companion_audio_vad_tracker_t *tracker, bool vad_active,
    bool voice_evidence_active, uint64_t now_ms)
{
    companion_audio_vad_result_t result = {0};
    if (NULL == tracker ||
        (tracker->has_last_update && now_ms < tracker->last_update_ms)) {
        return result;
    }
    tracker->last_update_ms = now_ms;
    tracker->has_last_update = true;
    if (!tracker->accepting_speech) {
        return result;
    }

    if (vad_active && !tracker->raw_active) {
        tracker->raw_active = true;
        tracker->window_started_ms = now_ms;
        if (tracker->suppress_current_window) {
            tracker->window_generation = 0U;
            tracker->window_wake_seq = 0U;
        } else {
            tracker->window_generation = tracker->capture_generation;
            tracker->window_wake_seq = tracker->capture_wake_seq;
        }
        if (window_is_current(tracker)) {
            tracker->tail_active = false;
            result.actions |= COMPANION_AUDIO_VAD_ACTION_START;
        }
    }

    if (vad_active && window_is_current(tracker)) {
        result.window_duration_ms = elapsed_ms(
            tracker->window_started_ms, now_ms);
    }

    const bool qualified_voice =
        vad_active && voice_evidence_active && window_is_current(tracker);
    if (qualified_voice && !tracker->voice_evidence_active) {
        tracker->voice_evidence_active = true;
        tracker->voice_window_started_ms = now_ms;
        tracker->tail_active = false;
        if (0U == tracker->accumulated_active_ms) {
            result.actions |= COMPANION_AUDIO_VAD_ACTION_START;
        }
    }

    if (qualified_voice) {
        const uint32_t voice_window_ms = elapsed_ms(
            tracker->voice_window_started_ms, now_ms);
        result.accumulated_active_ms = saturating_add(
            tracker->accumulated_active_ms,
            voice_window_ms);
        if (!tracker->speech_confirmed &&
            COMPANION_AUDIO_VAD_MIN_SPEECH_MS <=
                result.accumulated_active_ms) {
            tracker->speech_confirmed = true;
            result.actions |=
                COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED;
        }
    }

    if (!qualified_voice && tracker->voice_evidence_active) {
        const uint32_t voice_window_ms = elapsed_ms(
            tracker->voice_window_started_ms, now_ms);
        tracker->accumulated_active_ms = saturating_add(
            tracker->accumulated_active_ms, voice_window_ms);
        result.accumulated_active_ms = tracker->accumulated_active_ms;
        tracker->voice_evidence_active = false;
        tracker->voice_window_started_ms = 0ULL;
        if (!tracker->speech_confirmed &&
            COMPANION_AUDIO_VAD_MIN_SPEECH_MS <=
                tracker->accumulated_active_ms) {
            tracker->speech_confirmed = true;
            result.actions |=
                COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED;
        }
        tracker->tail_started_ms = now_ms;
        tracker->tail_active = true;
    }

    if (!vad_active && tracker->raw_active) {
        result.window_duration_ms = elapsed_ms(
            tracker->window_started_ms, now_ms);
        tracker->raw_active = false;
        tracker->window_generation = 0U;
        tracker->window_wake_seq = 0U;
        tracker->window_started_ms = 0ULL;
        tracker->suppress_current_window = false;
    }

    if (!qualified_voice && tracker->tail_active) {
        result.tail_silence_ms = elapsed_ms(
            tracker->tail_started_ms, now_ms);
        result.accumulated_active_ms = tracker->accumulated_active_ms;
        if (COMPANION_AUDIO_VAD_TAIL_SILENCE_MS <=
            result.tail_silence_ms) {
            if (tracker->speech_confirmed && capture_is_valid(tracker)) {
                result.actions |= COMPANION_AUDIO_VAD_ACTION_END;
            }
            reset_utterance(tracker);
        }
    }

    if (COMPANION_AUDIO_VAD_ACTION_NONE != result.actions) {
        result.generation = tracker->capture_generation;
        result.wake_seq = tracker->capture_wake_seq;
    }
    return result;
}

companion_audio_vad_result_t companion_audio_vad_tracker_step(
    companion_audio_vad_tracker_t *tracker, bool vad_active,
    uint64_t now_ms)
{
    return companion_audio_vad_tracker_step_with_voice(
        tracker, vad_active, vad_active, now_ms);
}
