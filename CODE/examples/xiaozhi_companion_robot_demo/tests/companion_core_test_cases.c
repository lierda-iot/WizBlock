#include "companion_core_test_cases.h"

#include "companion_core.h"
#include "companion_agent_binding_policy.h"
#include "companion_audio_pcm_queue.h"
#include "companion_audio_processor_policy.h"
#include "companion_audio_signal_metrics.h"
#include "companion_audio_vad_policy.h"
#include "companion_audio_voice_gate.h"
#include "audio_processor_task_policy.h"
#include "companion_controller_model.h"
#include "companion_controller_runtime_policy.h"
#include "companion_controller_stop_policy.h"
#include "companion_controller_wake_effects.h"
#include "companion_doa_estimator.h"
#include "companion_logic.h"
#include "companion_motion_result_policy.h"
#include "companion_touch_gesture.h"
#include "companion_turn_control.h"
#include "xiaozhi_agent_tts_barrier_policy.h"
#include "xiaozhi_agent_listen_mode_policy.h"
#include "xiaozhi_agent_vad_stop_policy.h"
#include "xiaozhi_agent_ws_start_policy.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


static int s_failure_count;
static int s_agent_retire_calls;
static int s_agent_cancel_calls;
static int s_stop_sequence[4];
static int s_stop_sequence_count;
static int s_wake_effect_sequence[2];
static int s_wake_effect_sequence_count;
static esp_err_t s_wake_agent_result;
static esp_err_t s_wake_motion_result;

static void expect_int(const char *const test_name, int expected, int actual);
static void expect_result(const char *const test_name, esp_err_t expected,
                          esp_err_t actual);
static void expect_true(const char *const test_name, bool actual);

static esp_err_t record_agent_retire(void *user_ctx)
{
    (void)user_ctx;
    s_agent_retire_calls++;
    s_stop_sequence[s_stop_sequence_count++] = 2;
    return ESP_OK;
}

static esp_err_t record_agent_cancel(void *user_ctx)
{
    (void)user_ctx;
    s_agent_cancel_calls++;
    s_stop_sequence[s_stop_sequence_count++] = 3;
    return ESP_OK;
}

static esp_err_t record_controller_retire(void *user_ctx)
{
    (void)user_ctx;
    s_stop_sequence[s_stop_sequence_count++] = 1;
    return ESP_OK;
}

static esp_err_t record_audio_stop(void *user_ctx)
{
    (void)user_ctx;
    s_stop_sequence[s_stop_sequence_count++] = 4;
    return ESP_OK;
}

static esp_err_t record_wake_agent(uint32_t generation, uint32_t wake_seq,
                                   void *user_ctx)
{
    (void)user_ctx;
    if (7U != generation || 8U != wake_seq) {
        return ESP_ERR_INVALID_ARG;
    }
    s_wake_effect_sequence[s_wake_effect_sequence_count++] = 1;
    return s_wake_agent_result;
}

static esp_err_t record_wake_motion(uint32_t generation, uint32_t wake_seq,
                                    void *user_ctx)
{
    (void)user_ctx;
    if (7U != generation || 8U != wake_seq) {
        return ESP_ERR_INVALID_ARG;
    }
    s_wake_effect_sequence[s_wake_effect_sequence_count++] = 2;
    return s_wake_motion_result;
}

static void run_ws_start_policy_tests(void)
{
    expect_int("WS start success does not retry",
               XIAOZHI_AGENT_WS_START_SUCCEEDED,
               xiaozhi_agent_ws_start_decide(ESP_OK, 1U));
    expect_int("WS start first failure retries",
               XIAOZHI_AGENT_WS_START_RETRY,
               xiaozhi_agent_ws_start_decide(ESP_FAIL, 1U));
    expect_int("WS start second failure retries",
               XIAOZHI_AGENT_WS_START_RETRY,
               xiaozhi_agent_ws_start_decide(ESP_FAIL, 2U));
    expect_int("WS start final failure gives up",
               XIAOZHI_AGENT_WS_START_GIVE_UP,
               xiaozhi_agent_ws_start_decide(ESP_FAIL, 3U));
    expect_int("WS start zero attempt gives up",
               XIAOZHI_AGENT_WS_START_GIVE_UP,
               xiaozhi_agent_ws_start_decide(ESP_FAIL, 0U));
}

static void run_audio_voice_gate_tests(void)
{
    const int16_t feature_pcm[] = {100, -100, 200, -200};
    companion_audio_voice_features_t extracted = {0};
    expect_true("Voice features extract deterministic PCM",
                companion_audio_voice_features_from_pcm(
                    feature_pcm, 4U, &extracted));
    expect_int("Voice features RMS", 158, (int)extracted.rms);
    expect_int("Voice features peak", 200, (int)extracted.peak);
    expect_int("Voice features ZCR", 1000,
               (int)extracted.zero_crossing_permille);

    companion_audio_voice_gate_t gate = {0};
    companion_audio_voice_gate_config_t config = {0};
    companion_audio_voice_gate_config_default(&config);
    companion_audio_voice_gate_init(&gate, &config);

    const companion_audio_voice_features_t quiet = {
        .rms = 100U,
        .peak = 260U,
        .zero_crossing_permille = 80U,
    };
    companion_audio_voice_gate_result_t result = {0};
    for (uint64_t now_ms = 0U; now_ms < 320U; now_ms += 40U) {
        result = companion_audio_voice_gate_step(
            &gate, false, &quiet, now_ms);
    }
    const companion_audio_voice_features_t distant_candidate = {
        .rms = 140U,
        .peak = 360U,
        .zero_crossing_permille = 90U,
    };
    result = companion_audio_voice_gate_step(
        &gate, true, &distant_candidate, 320U);
    expect_true("Voice gate rejects low-SNR VAD candidate",
                !result.evidence_active);

    const companion_audio_voice_features_t impulse_candidate = {
        .rms = 500U,
        .peak = 8000U,
        .zero_crossing_permille = 10U,
    };
    result = companion_audio_voice_gate_step(
        &gate, true, &impulse_candidate, 360U);
    expect_true("Voice gate rejects high-crest impulse",
                !result.evidence_active);

    const companion_audio_voice_features_t steady_noise = {
        .rms = 500U,
        .peak = 1000U,
        .zero_crossing_permille = 2U,
    };
    result = companion_audio_voice_gate_step(
        &gate, true, &steady_noise, 400U);
    expect_true("Voice gate rejects low-ZCR steady noise",
                !result.evidence_active);

    const companion_audio_voice_features_t near_voice = {
        .rms = 500U,
        .peak = 1800U,
        .zero_crossing_permille = 95U,
    };
    result = companion_audio_voice_gate_step(
        &gate, true, &near_voice, 440U);
    expect_true("Voice gate accepts high-SNR speech-shaped evidence",
                result.evidence_active);
}

static void run_audio_vad_policy_tests(void)
{
    companion_audio_vad_tracker_t candidate_only_tracker = {0};
    companion_audio_vad_tracker_arm(
        &candidate_only_tracker, 90U, 91U, false);
    companion_audio_vad_result_t candidate_only_result =
        companion_audio_vad_tracker_step_with_voice(
            &candidate_only_tracker, true, false, 1000U);
    expect_true("Raw VAD still publishes candidate start",
                0U != (candidate_only_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_START));
    candidate_only_result = companion_audio_vad_tracker_step_with_voice(
        &candidate_only_tracker, true, false, 1200U);
    expect_true("200ms raw VAD alone cannot confirm valid voice",
                0U == (candidate_only_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));

    companion_audio_vad_tracker_t qualified_tracker = {0};
    companion_audio_vad_tracker_arm(&qualified_tracker, 92U, 93U, false);
    (void)companion_audio_vad_tracker_step_with_voice(
        &qualified_tracker, true, true, 1000U);
    companion_audio_vad_result_t qualified_result =
        companion_audio_vad_tracker_step_with_voice(
            &qualified_tracker, true, true, 1200U);
    expect_true("Qualified voice evidence confirms speech",
                0U != (qualified_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    (void)companion_audio_vad_tracker_step_with_voice(
        &qualified_tracker, true, false, 1201U);
    qualified_result = companion_audio_vad_tracker_step_with_voice(
        &qualified_tracker, true, false, 2400U);
    expect_true("Rejected raw noise keeps 1199ms voice tail open",
                0U == (qualified_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));
    qualified_result = companion_audio_vad_tracker_step_with_voice(
        &qualified_tracker, true, false, 2401U);
    expect_true("Rejected raw noise cannot extend valid voice endpoint",
                0U != (qualified_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));

    companion_audio_vad_tracker_t prompt_tracker = {0};
    const companion_audio_vad_prompt_token_t prompt_token = {
        .generation = 1U,
        .wake_seq = 1U,
        .session_epoch = 0U,
        .request_id = 10U,
    };
    companion_audio_vad_tracker_arm_for_prompt(
        &prompt_tracker, 1U, 1U, false);
    expect_true("Prompt gate binds the current request",
                companion_audio_vad_tracker_prompt_started(
                    &prompt_tracker, &prompt_token));
    companion_audio_vad_result_t prompt_result =
        companion_audio_vad_tracker_step(&prompt_tracker, true, 1000U);
    expect_true("Prompt playback suppresses VAD start",
                COMPANION_AUDIO_VAD_ACTION_NONE == prompt_result.actions);
    prompt_result = companion_audio_vad_tracker_step(
        &prompt_tracker, true, 1200U);
    expect_true("Prompt playback suppresses speech confirmation",
                COMPANION_AUDIO_VAD_ACTION_NONE == prompt_result.actions);
    (void)companion_audio_vad_tracker_step(
        &prompt_tracker, false, 1201U);
    prompt_result = companion_audio_vad_tracker_step(
        &prompt_tracker, false, 2401U);
    expect_true("Prompt playback suppresses VAD end",
                COMPANION_AUDIO_VAD_ACTION_NONE == prompt_result.actions);
    expect_true("Current Prompt terminal requests fetch-owned reset",
                companion_audio_vad_tracker_prompt_terminal(
                    &prompt_tracker, &prompt_token));
    companion_audio_vad_prompt_token_t reset_token = {0};
    expect_true("Fetch owner takes the current Prompt reset",
                companion_audio_vad_tracker_take_prompt_reset(
                    &prompt_tracker, &reset_token));
    expect_int("Prompt reset preserves request identity", 10,
               (int)reset_token.request_id);
    prompt_result = companion_audio_vad_tracker_step(
        &prompt_tracker, true, 2500U);
    expect_true("Prompt gate stays closed until reset completes",
                COMPANION_AUDIO_VAD_ACTION_NONE == prompt_result.actions);
    expect_true("Fetch owner opens current Prompt after reset",
                companion_audio_vad_tracker_complete_prompt_reset(
                    &prompt_tracker, &reset_token));
    prompt_result = companion_audio_vad_tracker_step(
        &prompt_tracker, true, 3000U);
    expect_true("First post-Prompt fetch starts fresh speech",
                0U != (prompt_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_START));
    prompt_result = companion_audio_vad_tracker_step(
        &prompt_tracker, true, 3199U);
    expect_true("Post-Prompt speech stays unconfirmed at 199ms",
                0U == (prompt_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    prompt_result = companion_audio_vad_tracker_step(
        &prompt_tracker, true, 3200U);
    expect_true("Post-Prompt speech confirms at 200ms",
                0U != (prompt_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    prompt_result = companion_audio_vad_tracker_step(
        &prompt_tracker, true, 3201U);
    expect_true("Post-Prompt speech does not reconfirm at 201ms",
                0U == (prompt_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));

    companion_audio_vad_tracker_t replacement_tracker = {0};
    const companion_audio_vad_prompt_token_t old_prompt_token = {
        .generation = 2U,
        .wake_seq = 2U,
        .session_epoch = 20U,
        .request_id = 21U,
    };
    const companion_audio_vad_prompt_token_t new_prompt_token = {
        .generation = 3U,
        .wake_seq = 3U,
        .session_epoch = 30U,
        .request_id = 31U,
    };
    companion_audio_vad_tracker_arm_for_prompt(
        &replacement_tracker, 2U, 2U, false);
    expect_true("Old Prompt binds before replacement",
                companion_audio_vad_tracker_prompt_started(
                    &replacement_tracker, &old_prompt_token));
    expect_true("Old Prompt terminal becomes pending",
                companion_audio_vad_tracker_prompt_terminal(
                    &replacement_tracker, &old_prompt_token));
    reset_token = (companion_audio_vad_prompt_token_t){0};
    expect_true("Fetch takes old Prompt reset before replacement",
                companion_audio_vad_tracker_take_prompt_reset(
                    &replacement_tracker, &reset_token));
    companion_audio_vad_tracker_arm_for_prompt(
        &replacement_tracker, 3U, 3U, false);
    expect_true("Replacement Prompt binds new request",
                companion_audio_vad_tracker_prompt_started(
                    &replacement_tracker, &new_prompt_token));
    expect_true("Old Prompt reset cannot open replacement",
                !companion_audio_vad_tracker_complete_prompt_reset(
                    &replacement_tracker, &reset_token));
    expect_true("Old Prompt terminal cannot schedule replacement reset",
                !companion_audio_vad_tracker_prompt_terminal(
                    &replacement_tracker, &old_prompt_token));
    prompt_result = companion_audio_vad_tracker_step(
        &replacement_tracker, true, 4000U);
    expect_true("Replacement gate stays closed after stale terminal",
                COMPANION_AUDIO_VAD_ACTION_NONE == prompt_result.actions);

    companion_audio_vad_tracker_t request_tracker = {0};
    const companion_audio_vad_prompt_token_t request_41_token = {
        .generation = 4U,
        .wake_seq = 4U,
        .session_epoch = 40U,
        .request_id = 41U,
    };
    const companion_audio_vad_prompt_token_t request_42_token = {
        .generation = 4U,
        .wake_seq = 4U,
        .session_epoch = 40U,
        .request_id = 42U,
    };
    companion_audio_vad_tracker_arm_for_prompt(
        &request_tracker, 4U, 4U, false);
    expect_true("Original same-wake Prompt request binds",
                companion_audio_vad_tracker_prompt_started(
                    &request_tracker, &request_41_token));
    expect_true("Replacement same-wake Prompt request binds",
                companion_audio_vad_tracker_prompt_started(
                    &request_tracker, &request_42_token));
    expect_true("Old same-wake request terminal is rejected",
                !companion_audio_vad_tracker_prompt_terminal(
                    &request_tracker, &request_41_token));
    expect_true("Current same-wake request terminal is accepted",
                companion_audio_vad_tracker_prompt_terminal(
                    &request_tracker, &request_42_token));
    expect_true("Fetch takes current same-wake request reset",
                companion_audio_vad_tracker_take_prompt_reset(
                    &request_tracker, &reset_token));
    expect_true("Fetch completes current same-wake request reset",
                companion_audio_vad_tracker_complete_prompt_reset(
                    &request_tracker, &reset_token));

    companion_audio_vad_tracker_t endpoint_tracker = {0};
    companion_audio_vad_result_t endpoint_result = {0};
    companion_audio_vad_tracker_arm(&endpoint_tracker, 1U, 1U, false);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1000U);
    expect_true("VAD endpoint reports current speech start",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_START));
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1200U);
    expect_true("VAD endpoint confirms a 200ms short utterance",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 1201U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2401U);
    expect_true("VAD endpoint emits after 1200ms tail silence",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));

    endpoint_tracker = (companion_audio_vad_tracker_t){0};
    companion_audio_vad_tracker_arm(&endpoint_tracker, 2U, 2U, false);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1000U);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1100U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1199U);
    expect_true("VAD endpoint keeps 199ms below speech threshold",
                0U == (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    expect_int("VAD endpoint accumulates active deltas once", 199,
               (int)endpoint_result.accumulated_active_ms);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1200U);
    expect_true("VAD endpoint confirms at exactly 200ms",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    expect_int("VAD endpoint reports 200ms boundary", 200,
               (int)endpoint_result.accumulated_active_ms);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1201U);
    expect_true("VAD endpoint confirms speech only once",
                0U == (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    expect_int("VAD endpoint continues after confirmation", 201,
               (int)endpoint_result.accumulated_active_ms);

    endpoint_tracker = (companion_audio_vad_tracker_t){0};
    companion_audio_vad_tracker_arm(&endpoint_tracker, 3U, 3U, false);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1000U);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1200U);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 1201U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2400U);
    expect_true("VAD endpoint keeps 1199ms tail open",
                0U == (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));
    expect_int("VAD endpoint reports 1199ms tail", 1199,
               (int)endpoint_result.tail_silence_ms);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2401U);
    expect_true("VAD endpoint emits at exactly 1200ms tail",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2402U);
    expect_true("VAD endpoint does not repeat after 1201ms tail",
                0U == (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));

    endpoint_tracker = (companion_audio_vad_tracker_t){0};
    companion_audio_vad_tracker_arm(&endpoint_tracker, 4U, 4U, false);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1000U);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 1100U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2199U);
    expect_true("VAD endpoint keeps fragmented utterance tail open",
                0U == (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 2200U);
    expect_true("VAD endpoint resumes the same utterance within tail",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_START));
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 2300U);
    expect_true("VAD endpoint confirms accumulated fragments",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    expect_int("VAD endpoint accumulates two speech fragments", 200,
               (int)endpoint_result.accumulated_active_ms);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2301U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 3501U);
    expect_true("VAD endpoint ends resumed utterance after new tail",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));

    endpoint_tracker = (companion_audio_vad_tracker_t){0};
    companion_audio_vad_tracker_arm(&endpoint_tracker, 5U, 5U, false);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1000U);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 1199U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2399U);
    expect_true("VAD endpoint rejects isolated sub-threshold fragment",
                0U == (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 2400U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 2600U);
    expect_true("VAD endpoint rearms after rejected fragment",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    expect_int("VAD endpoint clears rejected fragment evidence", 200,
               (int)endpoint_result.accumulated_active_ms);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2601U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 3801U);
    expect_true("VAD endpoint emits first endpoint for rearm case",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));
    expect_int("VAD endpoint preserves capture generation after endpoint", 5,
               (int)endpoint_tracker.capture_generation);
    expect_int("VAD endpoint preserves capture wake after endpoint", 5,
               (int)endpoint_tracker.capture_wake_seq);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 4000U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 4200U);
    expect_true("VAD endpoint confirms second utterance on same capture",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 4201U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 5401U);
    expect_true("VAD endpoint emits again on same capture token",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));

    endpoint_tracker = (companion_audio_vad_tracker_t){0};
    companion_audio_vad_tracker_arm(&endpoint_tracker, 7U, 7U, false);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1000U);
    companion_audio_vad_tracker_arm(&endpoint_tracker, 8U, 8U, true);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1200U);
    expect_true("VAD endpoint rejects old active window after replacement",
                COMPANION_AUDIO_VAD_ACTION_NONE == endpoint_result.actions);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 1300U);
    expect_true("VAD endpoint rejects old window end after replacement",
                COMPANION_AUDIO_VAD_ACTION_NONE == endpoint_result.actions);
    expect_int("VAD endpoint preserves replacement generation", 8,
               (int)endpoint_tracker.capture_generation);
    expect_int("VAD endpoint preserves replacement wake sequence", 8,
               (int)endpoint_tracker.capture_wake_seq);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1400U);
    expect_true("VAD endpoint starts replacement window",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_START));
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1600U);
    expect_true("VAD endpoint confirms replacement window",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 1601U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2801U);
    expect_true("VAD endpoint emits replacement token endpoint",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));
    expect_int("VAD endpoint result uses replacement generation", 8,
               (int)endpoint_result.generation);
    expect_int("VAD endpoint result uses replacement wake sequence", 8,
               (int)endpoint_result.wake_seq);

    endpoint_tracker = (companion_audio_vad_tracker_t){0};
    companion_audio_vad_tracker_arm(&endpoint_tracker, 9U, 9U, true);
    companion_audio_vad_tracker_arm(&endpoint_tracker, 10U, 10U, false);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1000U);
    expect_true("VAD endpoint latest quiet arm clears old suppression",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_START));
    expect_int("VAD endpoint latest arm owns generation", 10,
               (int)endpoint_result.generation);
    expect_int("VAD endpoint latest arm owns wake sequence", 10,
               (int)endpoint_result.wake_seq);

    endpoint_tracker = (companion_audio_vad_tracker_t){0};
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 1000U);
    expect_true("VAD endpoint rejects speech without capture token",
                COMPANION_AUDIO_VAD_ACTION_NONE == endpoint_result.actions);

    endpoint_tracker = (companion_audio_vad_tracker_t){0};
    companion_audio_vad_tracker_arm(&endpoint_tracker, 11U, 11U, false);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 2000U);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, true, 2200U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2199U);
    expect_true("VAD endpoint rejects regressed timestamp",
                COMPANION_AUDIO_VAD_ACTION_NONE == endpoint_result.actions);
    expect_true("VAD endpoint preserves active window on time regression",
                endpoint_tracker.raw_active);
    (void)companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 2201U);
    endpoint_result = companion_audio_vad_tracker_step(
        &endpoint_tracker, false, 3401U);
    expect_true("VAD endpoint completes after monotonic time recovers",
                0U != (endpoint_result.actions &
                       COMPANION_AUDIO_VAD_ACTION_END));

    companion_audio_vad_tracker_t retired_tracker = {0};
    const companion_audio_vad_prompt_token_t retired_token = {
        .generation = 12U,
        .wake_seq = 12U,
        .session_epoch = 120U,
        .request_id = 121U,
    };
    companion_audio_vad_tracker_arm_for_prompt(
        &retired_tracker, 12U, 12U, false);
    expect_true("Retirement binds the current upload token",
                companion_audio_vad_tracker_prompt_started(
                    &retired_tracker, &retired_token));
    expect_true("Retirement test requests Prompt reset",
                companion_audio_vad_tracker_prompt_terminal(
                    &retired_tracker, &retired_token));
    expect_true("Retirement test takes Prompt reset",
                companion_audio_vad_tracker_take_prompt_reset(
                    &retired_tracker, &reset_token));
    expect_true("Retirement test completes Prompt reset",
                companion_audio_vad_tracker_complete_prompt_reset(
                    &retired_tracker, &reset_token));
    expect_true("Retirement test binds the live session epoch",
                companion_audio_vad_tracker_bind_upload_token(
                    &retired_tracker, &retired_token));
    (void)companion_audio_vad_tracker_step(&retired_tracker, true, 5000U);
    expect_true("VAD tracker retires the current request token",
                companion_audio_vad_tracker_retire(
                    &retired_tracker,
                    retired_token.generation,
                    retired_token.wake_seq,
                    retired_token.session_epoch,
                    retired_token.request_id));
    endpoint_result = companion_audio_vad_tracker_step(
        &retired_tracker, true, 5200U);
    expect_true("Retired request emits no stale VAD event",
                COMPANION_AUDIO_VAD_ACTION_NONE == endpoint_result.actions);
    expect_int("Retired request clears capture generation", 0,
               (int)retired_tracker.capture_generation);
    expect_true("Mismatched request token cannot retire active tracker",
                !companion_audio_vad_tracker_retire(
                    &retired_tracker,
                    retired_token.generation,
                    retired_token.wake_seq,
                    retired_token.session_epoch,
                    retired_token.request_id + 1U));

    companion_audio_vad_tracker_t active_retire_tracker = {0};
    const companion_audio_vad_prompt_token_t active_retire_token = {
        .generation = 13U,
        .wake_seq = 13U,
        .session_epoch = 130U,
        .request_id = 131U,
    };
    companion_audio_vad_tracker_arm_for_prompt(
        &active_retire_tracker, 13U, 13U, false);
    expect_true("Active retirement tracker binds a request",
                companion_audio_vad_tracker_prompt_started(
                    &active_retire_tracker, &active_retire_token));
    expect_true("Wrong session cannot retire active tracker",
                !companion_audio_vad_tracker_retire(
                    &active_retire_tracker, 13U, 13U, 131U, 131U));
    expect_int("Wrong token preserves active capture generation", 13,
               (int)active_retire_tracker.capture_generation);
    expect_true("Wrong request cannot retire active tracker",
                !companion_audio_vad_tracker_retire(
                    &active_retire_tracker, 13U, 13U, 130U, 132U));
    expect_true("Current request still retires after stale token attempts",
                companion_audio_vad_tracker_retire(
                    &active_retire_tracker,
                    active_retire_token.generation,
                    active_retire_token.wake_seq,
                    active_retire_token.session_epoch,
                    active_retire_token.request_id));

}

static void run_audio_pcm_queue_tests(void)
{
    static companion_audio_pcm_frame_t storage[
        COMPANION_AUDIO_PCM_QUEUE_DEPTH];
    companion_audio_pcm_queue_t queue = {0};
    static int16_t samples[COMPANION_AUDIO_PCM_FRAME_SAMPLES] = {0};
    const companion_audio_token_t token = {
        .generation = 7U,
        .wake_seq = 8U,
        .session_epoch = 9U,
        .request_id = 10U,
    };

    expect_result("PCM queue initializes", ESP_OK,
                  companion_audio_pcm_queue_init(
                      &queue, storage, COMPANION_AUDIO_PCM_QUEUE_DEPTH));
    for (size_t index = 0U;
         index < COMPANION_AUDIO_PCM_QUEUE_DEPTH - 1U; ++index) {
        samples[0] = (int16_t)index;
        expect_result("PCM queue accepts below capacity", ESP_OK,
                      companion_audio_pcm_queue_push(
                          &queue, samples, &token));
    }
    expect_int("PCM queue N-1 depth",
               (int)COMPANION_AUDIO_PCM_QUEUE_DEPTH - 1,
               (int)companion_audio_pcm_queue_count(&queue));
    samples[0] = 77;
    expect_result("PCM queue accepts Nth frame", ESP_OK,
                  companion_audio_pcm_queue_push(&queue, samples, &token));
    expect_int("PCM queue N depth",
               (int)COMPANION_AUDIO_PCM_QUEUE_DEPTH,
               (int)companion_audio_pcm_queue_count(&queue));
    expect_result("PCM queue rejects N+1 without blocking",
                  ESP_ERR_TIMEOUT,
                  companion_audio_pcm_queue_push(&queue, samples, &token));
    expect_int("PCM queue high-water remains N",
               (int)COMPANION_AUDIO_PCM_QUEUE_DEPTH,
               (int)queue.high_water);
    expect_int("PCM queue counts one overflow", 1,
               (int)queue.drops);

    companion_audio_pcm_frame_t frame = {0};
    expect_result("PCM queue pops oldest frame", ESP_OK,
                  companion_audio_pcm_queue_pop(&queue, &frame));
    expect_int("PCM queue preserves FIFO samples", 0,
               (int)frame.samples[0]);
    expect_int("PCM queue preserves request token", 10,
               (int)frame.token.request_id);
    companion_audio_pcm_queue_clear(&queue);
    expect_int("PCM queue clear resets depth", 0,
               (int)companion_audio_pcm_queue_count(&queue));
}

static void run_agent_vad_stop_policy_tests(void)
{
    xiaozhi_agent_vad_stop_policy_t policy = {0};
    xiaozhi_agent_vad_stop_policy_reset(&policy, 1U);
    expect_true("Agent VAD rejects event before LISTENING",
                !xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 1U));
    expect_true("Agent VAD arms current LISTENING request",
                xiaozhi_agent_vad_stop_policy_set_listening(
                    &policy, 1U, true));

    for (uint32_t index = 0U;
         index < XIAOZHI_AGENT_VAD_MIN_TX_FRAMES - 1U; ++index) {
        expect_true("Agent counts successful TX below stop boundary",
                    !xiaozhi_agent_vad_stop_policy_note_tx_success(
                        &policy, 1U));
    }
    expect_true("Agent accepts VAD with 19 successful TX frames",
                xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 1U));
    expect_true("Agent keeps stop pending below 20 frames",
                !xiaozhi_agent_vad_stop_policy_ready(&policy, 1U));
    expect_true("Agent accepts duplicate VAD idempotently while pending",
                xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 1U));
    expect_true("20th successful TX makes stop ready",
                xiaozhi_agent_vad_stop_policy_note_tx_success(&policy, 1U));
    expect_true("Agent sends stop exactly once",
                xiaozhi_agent_vad_stop_policy_mark_stop_sent(&policy, 1U));
    expect_true("Agent rejects stop replay after send",
                !xiaozhi_agent_vad_stop_policy_mark_stop_sent(&policy, 1U));
    expect_true("Agent rejects VAD replay after stop",
                !xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 1U));
    expect_true("Agent rejects TX after stop",
                !xiaozhi_agent_vad_stop_policy_note_tx_success(&policy, 1U));

    xiaozhi_agent_vad_stop_policy_reset(&policy, 2U);
    expect_true("Agent arms replacement request",
                xiaozhi_agent_vad_stop_policy_set_listening(
                    &policy, 2U, true));
    for (uint32_t index = 0U;
         index < XIAOZHI_AGENT_VAD_MIN_TX_FRAMES; ++index) {
        expect_true("Agent counts 20 TX frames before VAD",
                    !xiaozhi_agent_vad_stop_policy_note_tx_success(
                        &policy, 2U));
    }
    expect_true("Agent accepts VAD after 20 successful TX frames",
                xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 2U));
    expect_true("VAD after 20 frames is immediately stop ready",
                xiaozhi_agent_vad_stop_policy_ready(&policy, 2U));

    xiaozhi_agent_vad_stop_policy_reset(&policy, 21U);
    expect_true("Agent arms 21-frame boundary request",
                xiaozhi_agent_vad_stop_policy_set_listening(
                    &policy, 21U, true));
    for (uint32_t index = 0U;
         index < XIAOZHI_AGENT_VAD_MIN_TX_FRAMES + 1U; ++index) {
        (void)xiaozhi_agent_vad_stop_policy_note_tx_success(&policy, 21U);
    }
    expect_true("Agent accepts VAD after 21 successful TX frames",
                xiaozhi_agent_vad_stop_policy_accept_vad_end(
                    &policy, 21U));
    expect_true("VAD after 21 frames is immediately stop ready",
                xiaozhi_agent_vad_stop_policy_ready(&policy, 21U));
    expect_true("21-frame request sends stop exactly once",
                xiaozhi_agent_vad_stop_policy_mark_stop_sent(
                    &policy, 21U));

    xiaozhi_agent_vad_stop_policy_reset(&policy, 3U);
    expect_true("Agent arms TX failure case",
                xiaozhi_agent_vad_stop_policy_set_listening(
                    &policy, 3U, true));
    for (uint32_t index = 0U;
         index < XIAOZHI_AGENT_VAD_MIN_TX_FRAMES - 1U; ++index) {
        (void)xiaozhi_agent_vad_stop_policy_note_tx_success(&policy, 3U);
    }
    expect_true("Agent holds pending VAD across failed TX",
                xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 3U) &&
                    !xiaozhi_agent_vad_stop_policy_ready(&policy, 3U));
    expect_int("Failed TX does not increment successful frame count",
               (int)(XIAOZHI_AGENT_VAD_MIN_TX_FRAMES - 1U),
               (int)policy.tx_success_frames);
    expect_true("Agent reaches stop after next successful TX",
                xiaozhi_agent_vad_stop_policy_note_tx_success(&policy, 3U));

    xiaozhi_agent_vad_stop_policy_reset(&policy, 4U);
    expect_true("Agent arms new request after cancellation",
                xiaozhi_agent_vad_stop_policy_set_listening(
                    &policy, 4U, true));
    expect_true("Old request VAD cannot affect replacement",
                !xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 3U));
    expect_true("Replacement request accepts VAD",
                xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 4U));

    xiaozhi_agent_vad_stop_policy_reset(&policy, 5U);
    expect_true("Agent arms resumed speech request",
                xiaozhi_agent_vad_stop_policy_set_listening(
                    &policy, 5U, true));
    for (uint32_t index = 0U; index < 9U; ++index) {
        (void)xiaozhi_agent_vad_stop_policy_note_tx_success(&policy, 5U);
    }
    expect_true("Early VAD end becomes pending",
                xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 5U));
    expect_true("Current VAD start cancels pending stop",
                xiaozhi_agent_vad_stop_policy_accept_vad_start(&policy, 5U));
    expect_true("Duplicate VAD start is idempotent",
                xiaozhi_agent_vad_stop_policy_accept_vad_start(&policy, 5U));
    for (uint32_t index = 9U;
         index < XIAOZHI_AGENT_VAD_MIN_TX_FRAMES; ++index) {
        expect_true("TX count survives resumed speech",
                    !xiaozhi_agent_vad_stop_policy_note_tx_success(
                        &policy, 5U));
    }
    expect_int("Resumed speech preserves all successful TX frames",
               (int)XIAOZHI_AGENT_VAD_MIN_TX_FRAMES,
               (int)policy.tx_success_frames);
    expect_true("Resumed speech is not stopped at frame threshold",
                !xiaozhi_agent_vad_stop_policy_ready(&policy, 5U));
    expect_true("Next VAD end arms stop after resumed speech",
                xiaozhi_agent_vad_stop_policy_accept_vad_end(&policy, 5U));
    expect_true("Next VAD end is immediately ready after threshold",
                xiaozhi_agent_vad_stop_policy_ready(&policy, 5U));
    expect_true("Resumed speech request sends stop once",
                xiaozhi_agent_vad_stop_policy_mark_stop_sent(&policy, 5U));
    expect_true("VAD start cannot reopen a stopped request",
                !xiaozhi_agent_vad_stop_policy_accept_vad_start(&policy, 5U));
    expect_true("Old request VAD start cannot affect stopped replacement",
                !xiaozhi_agent_vad_stop_policy_accept_vad_start(&policy, 4U));
}

static void run_audio_processor_policy_tests(void)
{
    const audio_processor_config_t companion_config =
        companion_audio_make_processor_config();
    const companion_audio_task_policy_t task_policy =
        companion_audio_make_task_policy();
    expect_int("Companion playback stays on CPU1", 1,
               (int)task_policy.playback_core);
    expect_int("Companion feed stays on CPU0", 0,
               (int)task_policy.feed_core);
    expect_int("Companion fetch moves to CPU1", 1,
               (int)task_policy.fetch_core);
    expect_int("Companion encode stays on CPU1", 1,
               (int)task_policy.encode_core);
    expect_int("Companion playback priority remains 4", 4,
               (int)task_policy.playback_priority);
    expect_int("Companion feed priority remains 5", 5,
               (int)task_policy.feed_priority);
    expect_int("Companion fetch priority remains 6", 6,
               (int)task_policy.fetch_priority);
    expect_int("Companion encode priority remains 3", 3,
               (int)task_policy.encode_priority);
    expect_int("Companion AFE keeps two microphones", 2,
               (int)companion_config.mic_channels);
    expect_int("Companion AFE keeps one reference", 1,
               (int)companion_config.ref_channels);
    expect_true("Companion AFE keeps NS enabled", companion_config.enable_ns);
    expect_true("Companion AFE keeps AEC enabled", companion_config.enable_aec);
    expect_true("Companion AFE keeps VAD enabled", companion_config.enable_vad);
    expect_true("Companion AFE keeps WakeNet enabled",
                companion_config.enable_wakenet);
    expect_int("Companion WakeNet threshold remains 0.65", 650,
               (int)(companion_config.wakenet_threshold * 1000.0f));
    expect_int("Companion AEC remains VOIP low-cost mode", 3,
               companion_config.aec_mode);
    expect_int("Companion selects low-cost AFE only", 
               AUDIO_PROCESSOR_AFE_MODE_LOW_COST,
               audio_processor_resolve_afe_mode(&companion_config));
    expect_true("Companion pins AFE processing task policy",
                companion_config.afe_task_policy_valid &&
                    1U == companion_config.afe_task_core &&
                    5U == companion_config.afe_task_priority);
    const audio_processor_task_policy_t companion_task_policy = {
        .valid = companion_config.afe_task_policy_valid,
        .core = companion_config.afe_task_core,
        .priority = companion_config.afe_task_priority,
    };
    expect_true("Companion AFE task policy is valid",
                audio_processor_task_policy_is_valid(&companion_task_policy));
    const audio_processor_task_policy_t default_task_policy = {0};
    expect_true("Shared AFE task policy remains library default when unset",
                audio_processor_task_policy_is_valid(&default_task_policy));
    const audio_processor_task_policy_t invalid_task_policy = {
        .valid = true,
        .core = 2U,
        .priority = 5U,
    };
    expect_true("Invalid AFE task core is rejected",
                !audio_processor_task_policy_is_valid(&invalid_task_policy));
    const audio_processor_task_policy_t invalid_priority_policy = {
        .valid = true,
        .core = 1U,
        .priority = 0U,
    };
    expect_true("Invalid AFE task priority is rejected",
                !audio_processor_task_policy_is_valid(&invalid_priority_policy));

    const audio_processor_config_t default_config = {0};
    expect_int("Shared AFE default remains high-performance",
               AUDIO_PROCESSOR_AFE_MODE_HIGH_PERF,
               audio_processor_resolve_afe_mode(&default_config));
    audio_processor_config_t explicit_high = {0};
    explicit_high.afe_mode = AUDIO_PROCESSOR_AFE_MODE_HIGH_PERF;
    expect_int("Shared AFE accepts explicit high-performance",
               AUDIO_PROCESSOR_AFE_MODE_HIGH_PERF,
               audio_processor_resolve_afe_mode(&explicit_high));
    audio_processor_config_t invalid_config = {0};
    invalid_config.afe_mode = (audio_processor_afe_mode_t)99;
    expect_int("Invalid AFE mode falls back to high-performance",
               AUDIO_PROCESSOR_AFE_MODE_HIGH_PERF,
               audio_processor_resolve_afe_mode(&invalid_config));
    expect_int("Null AFE config falls back to high-performance",
               AUDIO_PROCESSOR_AFE_MODE_HIGH_PERF,
               audio_processor_resolve_afe_mode(NULL));
}

static void run_audio_signal_metrics_tests(void)
{
    enum { TEST_FRAMES = 512, TEST_LAG = 32 };
    int16_t mmr[TEST_FRAMES * 3] = {0};
    uint32_t seed = 1U;
    for (size_t frame = 0U; frame < TEST_FRAMES; ++frame) {
        seed = seed * 1664525U + 1013904223U;
        mmr[frame * 3U + 2U] =
            (int16_t)((int32_t)((seed >> 16U) & 0x3fffU) - 8192);
    }
    for (size_t frame = TEST_LAG; frame < TEST_FRAMES; ++frame) {
        mmr[frame * 3U] = mmr[(frame - TEST_LAG) * 3U + 2U];
        mmr[frame * 3U + 1U] =
            (int16_t)(mmr[(frame - TEST_LAG) * 3U + 2U] / 2);
    }
    companion_audio_signal_metrics_t metrics = {0};
    expect_true("Synthetic MMR PCM is measurable",
                companion_audio_signal_measure_mmr(
                    mmr, TEST_FRAMES, TEST_LAG, &metrics));
    expect_true("Synthetic MIC1 identifies configured REF delay",
                0.99 < metrics.mic1_ref_correlation);
    expect_true("Synthetic MIC2 identifies configured REF delay",
                0.99 < metrics.mic2_ref_correlation);
    expect_int("Synthetic signal starts without clipping", 0,
               (int)(metrics.mic1_clipped_samples +
                     metrics.mic2_clipped_samples +
                     metrics.ref_clipped_samples));

    mmr[100U * 3U] = INT16_MAX;
    mmr[101U * 3U + 1U] = INT16_MIN;
    expect_true("Clipped MMR PCM remains measurable",
                companion_audio_signal_measure_mmr(
                    mmr, TEST_FRAMES, TEST_LAG, &metrics));
    expect_int("PCM metrics count MIC clipping", 2,
               (int)(metrics.mic1_clipped_samples +
                     metrics.mic2_clipped_samples));

    const companion_audio_realtime_window_t baseline = {
        .captured_samples = 280U,
        .expected_samples = 1000U,
        .processor_max_us = 194000U,
        .block_period_us = 64000U,
        .mic_clipped_samples = 1U,
    };
    const uint32_t baseline_flags =
        companion_audio_evaluate_realtime_window(&baseline);
    expect_true("Current log baseline detects capture deficit",
                0U != (baseline_flags &
                       COMPANION_AUDIO_SIGNAL_CAPTURE_DEFICIT));
    expect_true("Current log baseline detects processor overrun",
                0U != (baseline_flags &
                       COMPANION_AUDIO_SIGNAL_PROCESSOR_OVERRUN));
    expect_true("Current log baseline detects MIC clipping",
                0U != (baseline_flags &
                       COMPANION_AUDIO_SIGNAL_MIC_CLIPPING));

    const companion_audio_realtime_window_t acceptable = {
        .captured_samples = 950U,
        .expected_samples = 1000U,
        .processor_max_us = 60000U,
        .block_period_us = 64000U,
        .mic_clipped_samples = 0U,
    };
    expect_int("Realtime acceptance window has no diagnostic flags", 0,
               (int)companion_audio_evaluate_realtime_window(&acceptable));
}

static void run_agent_binding_policy_tests(void)
{
    const companion_agent_binding_id_t current = {
        .generation = 2U,
        .wake_seq = 2U,
        .request_id = 12U,
    };
    expect_int("Binding accepts current PLAY",
               COMPANION_AGENT_BINDING_CURRENT,
               companion_agent_binding_route_audio(
                   &current, 12U));
    expect_int("Binding accepts current STOP",
               COMPANION_AGENT_BINDING_CURRENT,
               companion_agent_binding_route_audio(
                   &current, 12U));
    expect_int("Binding rejects retired STOP",
               COMPANION_AGENT_BINDING_DROP,
               companion_agent_binding_route_audio(
                   &current, 11U));
    expect_int("Binding rejects retired PLAY",
               COMPANION_AGENT_BINDING_DROP,
               companion_agent_binding_route_audio(
                   &current, 11U));
    expect_int("Binding rejects unknown request",
               COMPANION_AGENT_BINDING_DROP,
               companion_agent_binding_route_audio(
                   &current, 10U));
    const companion_agent_binding_id_t retired = {0};
    expect_int("Binding rejects audio after controller retirement",
               COMPANION_AGENT_BINDING_DROP,
               companion_agent_binding_route_audio(
                   &retired, current.request_id));
    companion_agent_binding_id_t replacement = current;
    expect_true("Current Adapter binding can be retired",
                companion_agent_binding_retire_if_current(
                    &replacement, &current));
    expect_int("Adapter retirement clears request token", 0,
               (int)replacement.request_id);
    replacement = current;
    const companion_agent_binding_id_t stale = {
        .generation = 1U,
        .wake_seq = 1U,
        .request_id = 11U,
    };
    expect_true("Stale token cannot retire replacement Adapter binding",
                !companion_agent_binding_retire_if_current(
                    &replacement, &stale));
    expect_int("Stale retirement preserves replacement request", 12,
               (int)replacement.request_id);
}

static void run_controller_stop_policy_tests(void)
{
    companion_controller_stop_plan_t plan = {0};
    companion_controller_stop_reason_t reason =
        COMPANION_CONTROLLER_STOP_REPLACEMENT_WAKE;
    expect_result("idle wake stop reason is valid", ESP_OK,
                  companion_controller_wake_stop_reason(
                      COMPANION_PRODUCT_IDLE, &reason));
    expect_int("idle wake keeps full session-stop semantics",
               COMPANION_CONTROLLER_STOP_SESSION, reason);
    const companion_product_state_t active_states[] = {
        COMPANION_PRODUCT_CONNECTING,
        COMPANION_PRODUCT_LISTENING,
        COMPANION_PRODUCT_PROCESSING,
        COMPANION_PRODUCT_SPEAKING,
    };
    for (size_t index = 0U;
         index < sizeof(active_states) / sizeof(active_states[0]);
         ++index) {
        expect_result("active wake stop reason is valid", ESP_OK,
                      companion_controller_wake_stop_reason(
                          active_states[index], &reason));
        expect_int("active wake preserves healthy transport",
                   COMPANION_CONTROLLER_STOP_REPLACEMENT_WAKE, reason);
    }
    expect_result("locating wake stop reason is rejected",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_wake_stop_reason(
                      COMPANION_PRODUCT_LOCATING, &reason));
    expect_result("replacement wake stop plan is valid", ESP_OK,
                  companion_controller_stop_plan_build(
                      COMPANION_CONTROLLER_STOP_REPLACEMENT_WAKE, &plan));
    expect_int("replacement wake retires binding without Agent cancel",
               COMPANION_CONTROLLER_AGENT_STOP_RETIRE_BINDING,
               plan.agent_action);
    const companion_controller_agent_stop_ops_t ops = {
        .retire_controller_binding = record_controller_retire,
        .retire_binding = record_agent_retire,
        .cancel_request = record_agent_cancel,
        .stop_audio = record_audio_stop,
    };
    s_agent_retire_calls = 0;
    s_agent_cancel_calls = 0;
    s_stop_sequence_count = 0;
    expect_result("replacement wake stop effects execute", ESP_OK,
                  companion_controller_agent_stop_execute(
                      COMPANION_CONTROLLER_STOP_REPLACEMENT_WAKE,
                      &ops, NULL));
    expect_int("replacement wake executes one binding retirement", 1,
               s_agent_retire_calls);
    expect_int("replacement wake executes no Agent cancel", 0,
               s_agent_cancel_calls);
    expect_int("replacement wake executes three ordered effects", 3,
               s_stop_sequence_count);
    expect_true("replacement wake retires both bindings before audio stop",
                1 == s_stop_sequence[0] && 2 == s_stop_sequence[1] &&
                    4 == s_stop_sequence[2]);
    s_agent_retire_calls = 0;
    s_agent_cancel_calls = 0;
    s_stop_sequence_count = 0;
    expect_result("ordinary session stop effects execute", ESP_OK,
                  companion_controller_agent_stop_execute(
                      COMPANION_CONTROLLER_STOP_SESSION, &ops, NULL));
    expect_int("ordinary session stop does not use retire-only path", 0,
               s_agent_retire_calls);
    expect_int("ordinary session stop executes one Agent cancel", 1,
               s_agent_cancel_calls);
    expect_int("ordinary session stop executes three ordered effects", 3,
               s_stop_sequence_count);
    expect_true("ordinary stop retires Controller before cancel and audio",
                1 == s_stop_sequence[0] && 3 == s_stop_sequence[1] &&
                    4 == s_stop_sequence[2]);
    expect_result("agent plane stop plan is valid", ESP_OK,
                  companion_controller_stop_plan_build(
                      COMPANION_CONTROLLER_STOP_AGENT_PLANE, &plan));
    expect_true("agent plane stop preserves DOA", !plan.cancel_doa);
    expect_true("agent plane stop preserves motion", !plan.stop_motion);
}

static void run_controller_runtime_policy_tests(void)
{
    companion_controller_tts_activity_fact_t activity = {0};
    expect_result("first current TTS activity creates latest fact", ESP_OK,
                  companion_controller_tts_activity_record(
                      &activity, 1U, 2U, 3U, 4U, 100U));
    expect_true("first TTS activity fact is valid", activity.valid);
    expect_int("first TTS activity revision", 1,
               (int)activity.revision);
    expect_true("first TTS activity timestamp is retained",
                100U == activity.timestamp_ms);
    expect_result("same-token TTS activity at 999ms is coalesced", ESP_OK,
                  companion_controller_tts_activity_record(
                      &activity, 1U, 2U, 3U, 4U, 1099U));
    expect_int("coalesced TTS activity keeps revision", 1,
               (int)activity.revision);
    expect_true("coalesced TTS activity keeps accepted timestamp",
                100U == activity.timestamp_ms);
    expect_result("same-token TTS activity at 1000ms publishes", ESP_OK,
                  companion_controller_tts_activity_record(
                      &activity, 1U, 2U, 3U, 4U, 1100U));
    expect_int("1000ms TTS activity advances revision", 2,
               (int)activity.revision);
    expect_true("1000ms TTS activity updates timestamp",
                1100U == activity.timestamp_ms);
    expect_result("regressed same-token TTS timestamp is rejected",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_tts_activity_record(
                      &activity, 1U, 2U, 3U, 4U, 1099U));
    expect_int("rejected TTS activity keeps revision", 2,
               (int)activity.revision);
    expect_result("replacement token first TTS activity publishes immediately",
                  ESP_OK,
                  companion_controller_tts_activity_record(
                      &activity, 2U, 3U, 4U, 5U, 10U));
    expect_int("replacement TTS activity advances revision", 3,
               (int)activity.revision);
    expect_true("replacement TTS activity stores new token",
                2U == activity.generation && 3U == activity.wake_seq &&
                4U == activity.session_epoch && 5U == activity.request_id);

    expect_true("idle running roam is valid",
                companion_controller_roam_runtime_is_valid(
                    COMPANION_PRODUCT_IDLE, true, false, true));
    expect_true("reserved wake may briefly overlap running roam",
                companion_controller_roam_runtime_is_valid(
                    COMPANION_PRODUCT_IDLE, true, true, true));
    expect_true("running roam is invalid outside idle",
                !companion_controller_roam_runtime_is_valid(
                    COMPANION_PRODUCT_CONNECTING, true, true, true));
    expect_true("running roam is invalid when roam is disabled",
                !companion_controller_roam_runtime_is_valid(
                    COMPANION_PRODUCT_IDLE, false, true, true));
    expect_true("stopped roam has no runtime ownership conflict",
                companion_controller_roam_runtime_is_valid(
                    COMPANION_PRODUCT_CONNECTING, false, true, false));
}

static void run_controller_wake_effect_tests(void)
{
    const companion_controller_wake_effect_ops_t ops = {
        .start_agent = record_wake_agent,
        .start_motion = record_wake_motion,
    };
    companion_controller_wake_effect_result_t result = {0};

    s_wake_effect_sequence_count = 0;
    s_wake_agent_result = ESP_OK;
    s_wake_motion_result = ESP_OK;
    expect_result("wake effects submit both planes", ESP_OK,
                  companion_controller_wake_effects_execute(
                      &ops, 7U, 8U, NULL, &result));
    expect_int("wake effects submit exactly two effects", 2,
               s_wake_effect_sequence_count);
    expect_true("wake effects prioritize Agent before Motion",
                1 == s_wake_effect_sequence[0] &&
                    2 == s_wake_effect_sequence[1]);
    expect_result("wake Agent result is independent", ESP_OK,
                  result.agent_result);
    expect_result("wake Motion result is independent", ESP_OK,
                  result.motion_result);

    s_wake_effect_sequence_count = 0;
    s_wake_agent_result = ESP_FAIL;
    s_wake_motion_result = ESP_OK;
    expect_result("Agent failure does not stop wake dispatch", ESP_OK,
                  companion_controller_wake_effects_execute(
                      &ops, 7U, 8U, NULL, &result));
    expect_int("Motion still starts after Agent failure", 2,
               s_wake_effect_sequence_count);
    expect_result("Agent failure remains on Agent plane", ESP_FAIL,
                  result.agent_result);
    expect_result("Motion succeeds after Agent failure", ESP_OK,
                  result.motion_result);

    s_wake_effect_sequence_count = 0;
    s_wake_agent_result = ESP_OK;
    s_wake_motion_result = ESP_ERR_TIMEOUT;
    expect_result("Motion failure does not fail wake dispatch", ESP_OK,
                  companion_controller_wake_effects_execute(
                      &ops, 7U, 8U, NULL, &result));
    expect_int("Agent still starts before Motion failure", 2,
               s_wake_effect_sequence_count);
    expect_result("Agent succeeds before Motion failure", ESP_OK,
                  result.agent_result);
    expect_result("Motion failure remains on Motion plane",
                  ESP_ERR_TIMEOUT, result.motion_result);

    expect_result("wake effects reject zero generation",
                  ESP_ERR_INVALID_ARG,
                  companion_controller_wake_effects_execute(
                      &ops, 0U, 8U, NULL, &result));
}

static void run_tts_barrier_policy_tests(void)
{
    xiaozhi_agent_tts_barrier_t barrier = {0};
    expect_true("TTS barrier treats STT as processing evidence",
                xiaozhi_agent_tts_barrier_type_is_processing_evidence("stt"));
    expect_true("TTS barrier treats LLM as processing evidence",
                xiaozhi_agent_tts_barrier_type_is_processing_evidence("llm"));
    expect_true("TTS barrier rejects TTS as processing evidence",
                !xiaozhi_agent_tts_barrier_type_is_processing_evidence("tts"));
    expect_true("TTS barrier rejects null processing evidence type",
                !xiaozhi_agent_tts_barrier_type_is_processing_evidence(NULL));
    expect_true("TTS start enters speaking",
                xiaozhi_agent_tts_state_starts_speaking("start"));
    expect_true("TTS sentence start enters speaking",
                xiaozhi_agent_tts_state_starts_speaking("sentence_start"));
    expect_true("TTS sentence end does not enter speaking",
                !xiaozhi_agent_tts_state_starts_speaking("sentence_end"));
    expect_true("Null TTS state does not enter speaking",
                !xiaozhi_agent_tts_state_starts_speaking(NULL));
    xiaozhi_agent_tts_barrier_reset(&barrier, 11U, 22U);
    expect_true("TTS barrier starts closed",
                !xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 11U, 0U));
    xiaozhi_agent_tts_barrier_on_processing_evidence(&barrier);
    xiaozhi_agent_tts_barrier_on_listening_ready(&barrier);
    expect_true("TTS barrier stays closed after READY",
                !xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 11U, 22U));
    xiaozhi_agent_tts_barrier_on_listen_stop(&barrier);
    expect_true("TTS barrier stays closed without processing evidence",
                !xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 11U, 22U));
    xiaozhi_agent_tts_barrier_on_processing_evidence(&barrier);
    expect_true("TTS barrier rejects missing request token",
                !xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 11U, 0U));
    expect_true("TTS barrier accepts current request token",
                xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 11U, 22U));
    expect_true("TTS barrier rejects old epoch",
                !xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 10U, 22U));
    expect_true("TTS barrier rejects old request",
                !xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 11U, 21U));
    xiaozhi_agent_tts_barrier_reset(&barrier, 12U, 23U);
    expect_true("TTS barrier resets closed on new request",
                !xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 12U, 0U));
    xiaozhi_agent_tts_barrier_on_listening_ready(&barrier);
    xiaozhi_agent_tts_barrier_on_listen_stop(&barrier);
    xiaozhi_agent_tts_barrier_on_processing_evidence(&barrier);
    expect_true("TTS barrier opens for replacement request",
                xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 12U, 23U));
    xiaozhi_agent_tts_barrier_reset(&barrier, 0U, 0U);
    expect_true("TTS barrier invalid reset clears prior request",
                !xiaozhi_agent_tts_barrier_accepts(
                    &barrier, 12U, 23U));
}

static void run_listen_mode_policy_tests(void)
{
    expect_true("default Agent listen mode remains auto",
                0 == strcmp(
                    xiaozhi_agent_listen_start_fields(false),
                    "\"state\":\"start\",\"mode\":\"auto\""));
    expect_true("client-owned VAD stop uses manual listen mode",
                0 == strcmp(
                    xiaozhi_agent_listen_start_fields(true),
                    "\"state\":\"start\",\"mode\":\"manual\""));
}

static void expect_result(const char *const test_name, esp_err_t expected,
                          esp_err_t actual)
{
    if (expected == actual) {
        printf("PASS: %s\n", test_name);
        return;
    }

    printf("FAIL: %s expected=%d actual=%d\n", test_name,
           (int)expected, (int)actual);
    s_failure_count++;
}

static void expect_true(const char *const test_name, bool actual)
{
    if (actual) {
        printf("PASS: %s\n", test_name);
        return;
    }
    printf("FAIL: %s expected=true actual=false\n", test_name);
    s_failure_count++;
}

static void expect_int(const char *const test_name, int expected, int actual)
{
    if (expected == actual) {
        printf("PASS: %s\n", test_name);
        return;
    }
    printf("FAIL: %s expected=%d actual=%d\n", test_name, expected, actual);
    s_failure_count++;
}

/* Test-only adapters keep the cases focused on the closed model interface. */
static esp_err_t test_model_apply(companion_controller_model_t *model,
                                  const companion_controller_input_t *input,
                                  companion_controller_output_t *output)
{
    companion_controller_output_t ignored = {0};
    return companion_controller_model_apply(
        model, input, (NULL != output) ? output : &ignored);
}

static esp_err_t companion_controller_model_finish_startup(
    companion_controller_model_t *model, bool core_ready, uint64_t now_ms)
{
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_snapshot(model, &snapshot);
    if (core_ready) {
        const companion_capability_t core_capabilities[] = {
            COMPANION_CAPABILITY_AUDIO,
            COMPANION_CAPABILITY_AGENT,
        };
        for (size_t index = 0U;
             index < sizeof(core_capabilities) / sizeof(core_capabilities[0]);
             ++index) {
            const companion_capability_t capability =
                core_capabilities[index];
            if (0U == snapshot.capability_revisions[capability]) {
                const companion_controller_input_t capability_input = {
                    .type = COMPANION_CONTROLLER_INPUT_CAPABILITY_SNAPSHOT,
                    .now_ms = now_ms,
                    .data.capability = {
                        .capability = capability,
                        .available = true,
                        .error = ESP_OK,
                        .revision = 1U,
                    },
                };
                const esp_err_t capability_result = test_model_apply(
                    model, &capability_input, NULL);
                if (ESP_OK != capability_result) {
                    return capability_result;
                }
            }
        }
        companion_controller_model_snapshot(model, &snapshot);
        if (0U == snapshot.network_revision) {
            const companion_controller_input_t network_input = {
                .type = COMPANION_CONTROLLER_INPUT_NETWORK_SNAPSHOT,
                .now_ms = now_ms,
                .data.network = {
                    .ready = true,
                    .revision = 1U,
                },
            };
            const esp_err_t network_result = test_model_apply(
                model, &network_input, NULL);
            if (ESP_OK != network_result) {
                return network_result;
            }
        }
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_STARTUP_COMPLETE,
        .now_ms = now_ms,
        .data.startup.core_ready = core_ready,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_reserve_wake(
    companion_controller_model_t *model, uint64_t now_ms,
    uint32_t *generation, uint32_t *wake_seq)
{
    if (NULL == generation || NULL == wake_seq) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_RESERVE_WAKE,
        .now_ms = now_ms,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    if (ESP_OK == result) {
        *generation = output.generation;
        *wake_seq = output.wake_seq;
    }
    return result;
}

static esp_err_t companion_controller_model_mark_locating_started(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_LOCATING_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_doa(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool valid, float relative_deg,
    companion_controller_decision_t *decision)
{
    if (NULL == decision) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_DOA_COMPLETED,
        .generation = generation,
        .wake_seq = wake_seq,
        .data.doa = {
            .valid = valid,
            .relative_deg = relative_deg,
        },
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    *decision = output.decision;
    return result;
}

static esp_err_t companion_controller_model_mark_motion_started(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_MOTION_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_motion_done(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, companion_controller_decision_t *decision)
{
    if (NULL == decision) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_MOTION_COMPLETED,
        .generation = generation,
        .wake_seq = wake_seq,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    *decision = output.decision;
    return result;
}

static esp_err_t companion_controller_model_mark_agent_accepted(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint32_t request_id, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_AGENT_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
        .data.agent_accepted.request_id = request_id,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_finish_agent(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_AGENT_FINISHED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_agent_semantic(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint32_t session_epoch, uint32_t request_id,
    companion_agent_semantic_t semantic, esp_err_t result,
    uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_AGENT_SEMANTIC,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
        .data.agent = {
            .session_epoch = session_epoch,
            .request_id = request_id,
            .semantic = semantic,
            .result = result,
        },
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_tts_activity(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint32_t session_epoch, uint32_t request_id,
    uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_TTS_ACTIVITY,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
        .data.tts_activity = {
            .session_epoch = session_epoch,
            .request_id = request_id,
        },
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_speech_confirmed(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_SPEECH_CONFIRMED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_vad_end_with_agent_state(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool agent_listening, bool *notify_agent)
{
    if (NULL == notify_agent) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_VAD_END,
        .generation = generation,
        .wake_seq = wake_seq,
        .data.vad.agent_listening = agent_listening,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    *notify_agent = output.notify_agent;
    return result;
}

static esp_err_t companion_controller_model_on_vad_end(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool *notify_agent)
{
    return companion_controller_model_on_vad_end_with_agent_state(
        model, generation, wake_seq, false, notify_agent);
}

static esp_err_t companion_controller_model_on_vad_start_with_agent_state(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool agent_listening, bool *notify_agent)
{
    if (NULL == notify_agent) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_VAD_START,
        .generation = generation,
        .wake_seq = wake_seq,
        .data.vad.agent_listening = agent_listening,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    *notify_agent = output.notify_agent;
    return result;
}

static bool companion_controller_model_should_upload(
    const companion_controller_model_t *model, uint32_t generation)
{
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_snapshot(model, &snapshot);
    return snapshot.generation == generation && snapshot.upload_gate_open &&
           COMPANION_PRODUCT_LISTENING == snapshot.product_state;
}

static bool companion_controller_model_poll_deadline(
    companion_controller_model_t *model, uint64_t now_ms,
    companion_controller_deadline_t *expired)
{
    return companion_controller_model_tick(model, now_ms, expired);
}

static void companion_controller_model_set_network(
    companion_controller_model_t *model, bool ready, uint64_t now_ms)
{
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_snapshot(model, &snapshot);
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_NETWORK_SNAPSHOT,
        .now_ms = now_ms,
        .data.network = {
            .ready = ready,
            .revision = snapshot.network_revision + 1U,
        },
    };
    (void)test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_enter_error(
    companion_controller_model_t *model,
    companion_controller_error_reason_t reason,
    bool restart_required, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_ENTER_ERROR,
        .now_ms = now_ms,
        .data.error = {
            .reason = reason,
            .restart_required = restart_required,
        },
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_recover_error(
    companion_controller_model_t *model,
    companion_controller_error_reason_t reason,
    bool core_ready, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_RECOVER_ERROR,
        .now_ms = now_ms,
        .data.error = {
            .reason = reason,
            .core_ready = core_ready,
        },
    };
    return test_model_apply(model, &input, NULL);
}

static bool companion_controller_model_toggle_roam(
    companion_controller_model_t *model)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_SW3_CLICK,
    };
    companion_controller_output_t output = {0};
    return ESP_OK == test_model_apply(model, &input, &output) &&
           output.roam_enabled;
}

static void run_prompt_vad_controller_tests(void)
{
    companion_controller_model_t model = {0};
    companion_audio_vad_tracker_t tracker = {0};
    companion_audio_vad_result_t vad = {0};
    companion_audio_vad_prompt_token_t reset_token = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    bool notify_agent = false;

    companion_controller_model_init(&model, 100U);
    expect_result("Prompt/READY startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 101U));
    expect_result("Prompt/READY wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 102U, &generation, &wake_seq));
    companion_audio_vad_tracker_arm_for_prompt(
        &tracker, generation, wake_seq, false);
    const companion_audio_vad_prompt_token_t ready_first_token = {
        .generation = generation,
        .wake_seq = wake_seq,
        .session_epoch = 0U,
        .request_id = 401U,
    };
    expect_true("Prompt/READY current Prompt binds",
                companion_audio_vad_tracker_prompt_started(
                    &tracker, &ready_first_token));
    expect_result("Prompt/READY Agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 401U, 103U));
    expect_result("READY may arrive before Prompt terminal", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 501U, 401U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY,
                      ESP_OK, 104U));
    vad = companion_audio_vad_tracker_step(&tracker, true, 1000U);
    expect_true("READY does not bypass active Prompt gate",
                COMPANION_AUDIO_VAD_ACTION_NONE == vad.actions);
    vad = companion_audio_vad_tracker_step(&tracker, true, 1200U);
    expect_true("Prompt-active 200ms cannot confirm Controller speech",
                COMPANION_AUDIO_VAD_ACTION_NONE == vad.actions &&
                !model.listen_voice_confirmed);
    expect_true("Current terminal queues reset after READY",
                companion_audio_vad_tracker_prompt_terminal(
                    &tracker, &ready_first_token));
    expect_true("Fetch takes terminal after READY",
                companion_audio_vad_tracker_take_prompt_reset(
                    &tracker, &reset_token));
    expect_true("Fetch reset completes after READY",
                companion_audio_vad_tracker_complete_prompt_reset(
                    &tracker, &reset_token));
    vad = companion_audio_vad_tracker_step(&tracker, true, 2000U);
    expect_true("Active terminal window restarts after reset",
                0U != (vad.actions & COMPANION_AUDIO_VAD_ACTION_START));
    vad = companion_audio_vad_tracker_step(&tracker, true, 2199U);
    expect_true("Active terminal window does not inherit old 199ms",
                0U == (vad.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    vad = companion_audio_vad_tracker_step(&tracker, true, 2200U);
    expect_true("Fresh post-terminal 200ms confirms speech",
                0U != (vad.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    if (0U != (vad.actions &
               COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED)) {
        expect_result("Post-terminal confirmation advances Controller",
                      ESP_OK,
                      companion_controller_model_on_speech_confirmed(
                          &model, vad.generation, vad.wake_seq, 2200U));
    }
    expect_int("Post-terminal confirmation owns 300s deadline", 302200,
               (int)model.state_deadline_ms);
    (void)companion_audio_vad_tracker_step(&tracker, false, 2201U);
    vad = companion_audio_vad_tracker_step(&tracker, false, 3400U);
    expect_true("Post-terminal tail stays open at 1199ms",
                0U == (vad.actions & COMPANION_AUDIO_VAD_ACTION_END));
    vad = companion_audio_vad_tracker_step(&tracker, false, 3401U);
    expect_true("Post-terminal tail closes at 1200ms",
                0U != (vad.actions & COMPANION_AUDIO_VAD_ACTION_END));
    if (0U != (vad.actions & COMPANION_AUDIO_VAD_ACTION_END)) {
        expect_result("Post-terminal endpoint reaches Controller", ESP_OK,
                      companion_controller_model_on_vad_end(
                          &model, vad.generation, vad.wake_seq,
                          &notify_agent));
    }
    expect_true("Post-terminal endpoint notifies ready Agent",
                notify_agent);
    vad = companion_audio_vad_tracker_step(&tracker, false, 3402U);
    expect_true("Post-terminal tail does not repeat at 1201ms",
                0U == (vad.actions & COMPANION_AUDIO_VAD_ACTION_END));

    companion_controller_model_init(&model, 500U);
    tracker = (companion_audio_vad_tracker_t){0};
    expect_result("Terminal/READY startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 501U));
    expect_result("Terminal/READY wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 502U, &generation, &wake_seq));
    companion_audio_vad_tracker_arm_for_prompt(
        &tracker, generation, wake_seq, false);
    const companion_audio_vad_prompt_token_t terminal_first_token = {
        .generation = generation,
        .wake_seq = wake_seq,
        .session_epoch = 0U,
        .request_id = 402U,
    };
    expect_true("Terminal/READY current Prompt binds",
                companion_audio_vad_tracker_prompt_started(
                    &tracker, &terminal_first_token));
    expect_true("Prompt terminal may arrive before READY",
                companion_audio_vad_tracker_prompt_terminal(
                    &tracker, &terminal_first_token));
    expect_true("Fetch takes terminal before READY",
                companion_audio_vad_tracker_take_prompt_reset(
                    &tracker, &reset_token));
    expect_true("Fetch reset completes before READY",
                companion_audio_vad_tracker_complete_prompt_reset(
                    &tracker, &reset_token));
    (void)companion_audio_vad_tracker_step(&tracker, true, 6000U);
    vad = companion_audio_vad_tracker_step(&tracker, true, 6200U);
    expect_true("Post-terminal prelisten confirms at 200ms",
                0U != (vad.actions &
                       COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED));
    expect_result("Post-terminal prelisten reaches Controller", ESP_OK,
                  companion_controller_model_on_speech_confirmed(
                      &model, vad.generation, vad.wake_seq, 6200U));
    expect_result("Terminal/READY Agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 402U, 6201U));
    expect_result("READY may arrive after Prompt terminal", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 502U, 402U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY,
                      ESP_OK, 6202U));
    expect_int("Prelisten confirmation keeps 300s absolute deadline",
               306200, (int)model.state_deadline_ms);
}

static void run_behavior_tests(void)
{
    companion_roam_config_t config = {0};
    companion_action_plan_t action_plan = {0};
    companion_turn_plan_t turn_plan = {0};
    int left = 0;
    int right = 0;

    companion_roam_config_default(&config);
    expect_result("default roam configuration is valid", ESP_OK,
                  companion_roam_config_validate(&config));
    expect_int("default roam minimum stop is 3000ms", 3000,
               (int)config.min_stop_ms);
    expect_int("default roam maximum stop is 8000ms", 8000,
               (int)config.max_stop_ms);
    expect_int("default roam initial delay is 3000ms", 3000,
               (int)config.initial_delay_ms);
    expect_result("default roam produces an action", ESP_OK,
                  companion_behavior_plan(&config, 0U, 0U, 0U, &action_plan));
    expect_true("default roam never selects stop as movement",
                COMPANION_ACTION_STOP != action_plan.action);

    companion_roam_config_t invalid = config;
    for (int i = 0; i < COMPANION_ACTION_COUNT; ++i) {
        invalid.actions[i].weight = 0U;
    }
    expect_result("all-zero roam weights are rejected", ESP_ERR_INVALID_ARG,
                  companion_roam_config_validate(&invalid));
    invalid = config;
    invalid.actions[COMPANION_ACTION_FORWARD].min_duration_ms = 900U;
    invalid.actions[COMPANION_ACTION_FORWARD].max_duration_ms = 300U;
    expect_result("reversed duration range is rejected", ESP_ERR_INVALID_ARG,
                  companion_roam_config_validate(&invalid));
    invalid = config;
    invalid.initial_delay_ms = 0U;
    expect_result("zero roam initial delay is rejected", ESP_ERR_INVALID_ARG,
                  companion_roam_config_validate(&invalid));

    expect_result("forward track mapping is valid", ESP_OK,
                  companion_action_tracks(COMPANION_ACTION_FORWARD, &left, &right));
    expect_int("forward left track is 100 percent", 100, left);
    expect_int("forward right track is 100 percent", 100, right);
    expect_result("left turn track mapping is valid", ESP_OK,
                  companion_action_tracks(COMPANION_ACTION_TURN_LEFT, &left, &right));
    expect_int("left turn left track is reverse 100 percent", -100, left);
    expect_int("left turn right track is forward 100 percent", 100, right);

    expect_result("9 degree relative angle is valid", ESP_OK,
                  companion_turn_plan_from_relative(9.0f, &turn_plan));
    expect_int("9 degree relative angle stays in dead zone",
               COMPANION_TURN_NONE, turn_plan.direction);
    expect_result("10 degree relative angle is valid", ESP_OK,
                  companion_turn_plan_from_relative(10.0f, &turn_plan));
    expect_int("10 degree relative angle turns left",
               COMPANION_TURN_LEFT, turn_plan.direction);
    expect_int("10 degree turn uses minimum hard timeout", 1500,
               (int)turn_plan.duration_ms);
    expect_result("52.5 degree relative angle is valid", ESP_OK,
                  companion_turn_plan_from_relative(52.5f, &turn_plan));
    expect_int("52.5 degree turn hard timeout is rounded up to 10ms", 4880,
               (int)turn_plan.duration_ms);
    expect_result("negative 90 degree relative angle is valid", ESP_OK,
                  companion_turn_plan_from_relative(-90.0f, &turn_plan));
    expect_int("negative relative angle turns right",
               COMPANION_TURN_RIGHT, turn_plan.direction);
    expect_true("relative angle is limited to 90 degrees",
                fabsf(turn_plan.relative_deg + 90.0f) < 0.1f);
    expect_int("90 degree turn is limited to 8000ms", 8000,
               (int)turn_plan.duration_ms);
    expect_result("non-finite relative angle is rejected", ESP_ERR_INVALID_ARG,
                  companion_turn_plan_from_relative(NAN, &turn_plan));
}

static void run_expression_tests(void)
{
    companion_expression_model_t model = {0};
    companion_expression_snapshot_t snapshot = {0};

    companion_expression_init(&model);
    expect_result("idle expression state is accepted", ESP_OK,
                  companion_expression_set_product(&model, COMPANION_PRODUCT_IDLE));
    expect_result("idle expression can render", ESP_OK,
                  companion_expression_model_render(&model, 1000U, 1U,
                                                    &snapshot));
    expect_int("idle base expression is smile", COMPANION_EXPRESSION_SMILE,
               snapshot.base);

    expect_result("touch press is accepted", ESP_OK,
                  companion_expression_set_touch(&model, true));
    expect_result("touch expression can render", ESP_OK,
                  companion_expression_model_render(&model, 1100U, 0U,
                                                    &snapshot));
    expect_true("touch overrides base animation with pout",
                COMPANION_EXPRESSION_EFFECT_POUT_IN == snapshot.effect ||
                COMPANION_EXPRESSION_EFFECT_POUT_OUT == snapshot.effect);

    expect_result("speaking product state is accepted during touch", ESP_OK,
                  companion_expression_set_product(&model, COMPANION_PRODUCT_SPEAKING));
    expect_result("touch release is accepted", ESP_OK,
                  companion_expression_set_touch(&model, false));
    expect_result("post-touch expression can render", ESP_OK,
                  companion_expression_model_render(&model, 1400U, 0U,
                                                    &snapshot));
    expect_int("touch release restores latest speaking base",
               COMPANION_EXPRESSION_TALK, snapshot.base);
    expect_true("speaking expression animates mouth",
                COMPANION_EXPRESSION_EFFECT_MOUTH_OPEN == snapshot.effect ||
                COMPANION_EXPRESSION_EFFECT_MOUTH_CLOSED == snapshot.effect);
}

static void run_controller_tests(void)
{
    companion_controller_model_t model = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    companion_controller_decision_t decision = {0};
    companion_controller_deadline_t expired = {0};

    companion_controller_model_init(&model, 100U);
    expect_int("controller starts in booting", COMPANION_PRODUCT_BOOTING,
               model.product_state);
    expect_result("booting rejects wake", ESP_ERR_INVALID_STATE,
                  companion_controller_model_reserve_wake(
                      &model, 101U, &generation, &wake_seq));
    expect_result("startup completion enters idle", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 102U));
    expect_result("startup completion is one shot", ESP_ERR_INVALID_STATE,
                  companion_controller_model_finish_startup(
                      &model, true, 103U));

    expect_result("idle controller reserves wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 104U, &generation, &wake_seq));
    expect_int("wake reserve waits for accepted effect",
               COMPANION_PRODUCT_IDLE, model.product_state);
    expect_true("wake reserve is observable", model.wake_reserved);
    expect_true("idle wake requires localization",
                model.wake_requires_localization);
    expect_result("independent agent effect enters connecting", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 11U, 105U));
    expect_result("unavailable doa closes only motion", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, false, 0.0f, &decision));
    expect_int("unavailable doa never requests agent",
               COMPANION_CONTROLLER_DECISION_NONE, decision.type);
    expect_int("motion completion preserves connecting session",
               COMPANION_PRODUCT_CONNECTING, model.product_state);
    const uint64_t connecting_deadline = model.state_deadline_ms;
    expect_result("connecting semantic is diagnostic and idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_CONNECTING, ESP_OK, 106U));
    expect_true("connecting semantic does not extend deadline",
                connecting_deadline == model.state_deadline_ms);
    expect_result("zero agent epoch is rejected", ESP_ERR_INVALID_ARG,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 0U, 11U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 107U));
    expect_result("listening ready opens upload", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 108U));
    const uint64_t listening_deadline = model.state_deadline_ms;
    expect_result("duplicate listening ready is idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 109U));
    expect_true("duplicate ready does not extend deadline",
                listening_deadline == model.state_deadline_ms);
    expect_true("only current listening generation uploads",
                companion_controller_model_should_upload(&model, generation));
    bool notify_agent = false;
    expect_result("current listening VAD end is accepted", ESP_OK,
                  companion_controller_model_on_vad_end(
                      &model, generation, wake_seq, &notify_agent));
    expect_true("listening VAD end requests one agent stop", notify_agent);
    expect_result("processing closes upload", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 110U));
    expect_true("processing upload is closed",
                !companion_controller_model_should_upload(&model, generation));
    const uint64_t processing_deadline = model.state_deadline_ms;
    expect_result("duplicate processing is idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 111U));
    expect_true("duplicate processing does not extend deadline",
                processing_deadline == model.state_deadline_ms);
    expect_result("listening ready cannot regress processing",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK,
                      112U));
    expect_result("same request may converge to speaking", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK, 113U));
    const uint64_t speaking_deadline = model.state_deadline_ms;
    expect_result("duplicate speaking is idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK, 114U));
    expect_true("duplicate speaking does not extend deadline",
                speaking_deadline == model.state_deadline_ms);
    expect_result("different request cannot close current session",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 12U,
                      COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK, 112U));
    expect_result("current closed returns idle", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK, 113U));
    expect_true("closed invalidates completed generation",
                generation != model.generation);

    companion_controller_model_init(&model, 200U);
    expect_result("turn test startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 201U));
    expect_result("turn wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 202U, &generation, &wake_seq));
    expect_result("turn wake accepts independent agent first", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 31U, 203U));
    expect_result("accepted doa effect enters locating", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 204U));
    expect_result("non-dead-zone doa creates turn effect", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, -60.0f, &decision));
    expect_int("doa chooses wake turn",
               COMPANION_CONTROLLER_DECISION_START_TURN, decision.type);
    expect_int("locating Motion preserves connecting session state",
               COMPANION_PRODUCT_CONNECTING, model.product_state);
    expect_int("turn effect is not published before acceptance",
               COMPANION_CONTROLLER_MOTION_LOCATING, model.motion_state);
    expect_result("accepted motion effect enters turning", ESP_OK,
                  companion_controller_model_mark_motion_started(
                      &model, generation, wake_seq, 205U));
    expect_result("stale motion completion is rejected", ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_motion_done(
                      &model, generation + 1U, wake_seq, &decision));
    expect_result("current motion completion closes only motion", ESP_OK,
                  companion_controller_model_on_motion_done(
                      &model, generation, wake_seq, &decision));
    expect_int("motion completion never requests agent",
               COMPANION_CONTROLLER_DECISION_NONE, decision.type);
    expect_result("first session epoch binds to request", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 41U, 31U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 206U));
    expect_result("later epoch change is stale", ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 42U, 31U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 207U));

    companion_controller_model_init(&model, 300U);
    expect_result("prelisten VAD startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 301U));
    expect_result("prelisten VAD wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 302U, &generation, &wake_seq));
    expect_result("prelisten speech confirmation is accepted", ESP_OK,
                  companion_controller_model_on_speech_confirmed(
                      &model, generation, wake_seq, 302U));
    expect_true("prelisten speech confirmation is retained",
                model.listen_voice_confirmed);
    expect_int("prelisten speech confirmation keeps first timestamp", 302,
               (int)model.listen_voice_started_ms);
    notify_agent = true;
    expect_result("valid prelisten VAD is accepted", ESP_OK,
                  companion_controller_model_on_vad_end(
                      &model, generation, wake_seq, &notify_agent));
    expect_true("valid prelisten VAD is held until request ready",
                model.pending_vad_end && !notify_agent);
    notify_agent = true;
    expect_result("prelisten VAD start cancels held endpoint", ESP_OK,
                  companion_controller_model_on_vad_start_with_agent_state(
                      &model, generation, wake_seq, false, &notify_agent));
    expect_true("prelisten VAD start clears held endpoint",
                !model.pending_vad_end);
    expect_true("prelisten VAD start does not notify unready Agent",
                !notify_agent);
    expect_result("prelisten endpoint can be held again", ESP_OK,
                  companion_controller_model_on_vad_end(
                      &model, generation, wake_seq, &notify_agent));
    expect_true("rearmed prelisten endpoint is held",
                model.pending_vad_end && !notify_agent);
    expect_result("prelisten Agent effect accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 51U, 303U));
    notify_agent = false;
    expect_result("VAD before transport listening stays pending", ESP_OK,
                  companion_controller_model_on_vad_end(
                      &model, generation, wake_seq, &notify_agent));
    expect_true("VAD before transport listening remains pending",
                model.pending_vad_end && !notify_agent);
    expect_result("prelisten VAD reaches listening ready", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 61U, 51U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 304U));
    expect_int("prelisten speech owns utterance absolute deadline", 300302,
               (int)model.state_deadline_ms);
    notify_agent = false;
    expect_result("prelisten VAD replays after listening ready", ESP_OK,
                  companion_controller_model_on_vad_end_with_agent_state(
                      &model, generation, wake_seq, true, &notify_agent));
    expect_true("replayed prelisten VAD notifies agent", notify_agent);

    companion_controller_model_init(&model, 350U);
    expect_result("ready race startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 351U));
    expect_result("ready race wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 352U, &generation, &wake_seq));
    expect_result("ready race agent effect accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 52U, 353U));
    notify_agent = false;
    expect_result("VAD may be held only after transport listening",
                  ESP_OK,
                  companion_controller_model_on_vad_end_with_agent_state(
                      &model, generation, wake_seq, true, &notify_agent));
    expect_true("transport-listening VAD is pending",
                model.pending_vad_end && !notify_agent);
    notify_agent = false;
    expect_result("ready-race VAD start cancels pending end", ESP_OK,
                  companion_controller_model_on_vad_start_with_agent_state(
                      &model, generation, wake_seq, true, &notify_agent));
    expect_true("ready-race VAD start clears controller pending",
                !model.pending_vad_end);
    expect_true("ready-race VAD start notifies listening Agent",
                notify_agent);
    expect_result("ready-race VAD end can be armed again", ESP_OK,
                  companion_controller_model_on_vad_end_with_agent_state(
                      &model, generation, wake_seq, true, &notify_agent));
    expect_true("ready-race VAD end is pending again",
                model.pending_vad_end && !notify_agent);
    notify_agent = false;
    expect_result("stale VAD start is rejected", ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_vad_start_with_agent_state(
                      &model, generation - 1U, wake_seq, true,
                      &notify_agent));
    expect_true("stale VAD start preserves current pending end",
                model.pending_vad_end && !notify_agent);
    expect_result("ready race reaches listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 62U, 52U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 354U));
    notify_agent = false;
    expect_result("held VAD replays after ready", ESP_OK,
                  companion_controller_model_on_vad_end_with_agent_state(
                      &model, generation, wake_seq, true, &notify_agent));
    expect_true("held VAD replay notifies agent", notify_agent);

    companion_controller_model_init(&model, 350U);
    expect_result("ready speech deadline startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 351U));
    expect_result("ready speech deadline wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 352U, &generation, &wake_seq));
    expect_result("ready speech deadline agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 52U, 353U));
    expect_result("ready speech deadline enters listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 62U, 52U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 354U));
    expect_result("listening speech confirmation is accepted", ESP_OK,
                  companion_controller_model_on_speech_confirmed(
                      &model, generation, wake_seq, 355U));
    expect_int("listening speech confirmation sets absolute deadline", 300355,
               (int)model.state_deadline_ms);
    expect_result("duplicate listening speech confirmation is idempotent",
                  ESP_OK,
                  companion_controller_model_on_speech_confirmed(
                      &model, generation, wake_seq, 356U));
    expect_int("duplicate speech confirmation does not extend deadline",
               300355, (int)model.state_deadline_ms);
    expect_result("stale speech confirmation is rejected",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_speech_confirmed(
                      &model, generation - 1U, wake_seq, 357U));
    expect_int("stale speech confirmation preserves current deadline",
               300355, (int)model.state_deadline_ms);
    expect_result("speech-confirmed session terminal is accepted", ESP_OK,
                  companion_controller_model_finish_agent(
                      &model, generation, wake_seq, 358U));
    expect_true("session terminal clears speech confirmation",
                !model.listen_voice_confirmed);
    expect_int("session terminal clears speech confirmation timestamp", 0,
               (int)model.listen_voice_started_ms);

    companion_controller_model_init(&model, 600U);
    expect_result("first voice deadline startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 601U));
    expect_result("first voice deadline wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 602U, &generation, &wake_seq));
    expect_result("first voice deadline agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 53U, 603U));
    expect_result("first voice deadline enters listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 63U, 53U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 604U));
    expect_true("first voice deadline keeps 19999ms alive",
                !companion_controller_model_poll_deadline(
                    &model, 20603U, &expired));
    expect_true("first voice deadline expires at 20000ms",
                companion_controller_model_poll_deadline(
                    &model, 20604U, &expired));
    expect_int("first voice deadline reason",
               COMPANION_CONTROLLER_DEADLINE_LISTEN_FIRST_VOICE,
               expired.reason);

    companion_controller_model_init(&model, 700U);
    expect_result("utterance deadline startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 701U));
    expect_result("utterance deadline wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 702U, &generation, &wake_seq));
    expect_result("utterance deadline agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 54U, 703U));
    expect_result("utterance deadline enters listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 64U, 54U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 704U));
    expect_true("utterance confirmation before first-voice deadline is accepted",
                ESP_OK == companion_controller_model_on_speech_confirmed(
                    &model, generation, wake_seq, 20703U));
    expect_int("utterance confirmation supersedes first-voice deadline", 320703,
               (int)model.state_deadline_ms);
    expect_true("utterance deadline ignores old 20s point",
                !companion_controller_model_poll_deadline(
                    &model, 20704U, &expired));
    expect_true("utterance deadline expires at 300000ms",
                companion_controller_model_poll_deadline(
                    &model, 320703U, &expired));
    expect_int("utterance deadline reason",
               COMPANION_CONTROLLER_DEADLINE_LISTEN_UTTERANCE_ABSOLUTE,
               expired.reason);

    companion_controller_model_init(&model, 800U);
    expect_result("expired confirmation startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 801U));
    expect_result("expired confirmation wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 802U, &generation, &wake_seq));
    expect_result("expired confirmation agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 55U, 803U));
    expect_result("expired confirmation enters listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 65U, 55U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 804U));
    expect_result("confirmation at first voice deadline is rejected",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_speech_confirmed(
                      &model, generation, wake_seq, 20804U));
    expect_true("rejected expired confirmation is not retained",
                !model.listen_voice_confirmed);

    companion_controller_model_init(&model, 400U);
    expect_true("boot deadline enters error",
                companion_controller_model_poll_deadline(
                    &model, 30400U, &expired));
    expect_int("boot deadline state is error", COMPANION_PRODUCT_ERROR,
               model.product_state);

    companion_controller_model_init(&model, 500U);
    expect_result("deadline startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 501U));
    expect_result("deadline wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 502U, &generation, &wake_seq));
    expect_result("deadline skips doa", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, false, 0.0f, &decision));
    expect_result("deadline agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 71U, 503U));
    expect_true("connecting deadline expires",
                companion_controller_model_poll_deadline(
                    &model, 30503U, &expired));
    expect_int("connecting deadline stays owned until controller cleanup",
               COMPANION_PRODUCT_CONNECTING,
               model.product_state);
    const uint32_t expired_generation = expired.generation;
    const uint32_t expired_wake_seq = expired.wake_seq;
    expect_result("connecting deadline does not enable wake replacement",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_reserve_wake(
                      &model, 30504U, &generation, &wake_seq));
    expect_result("connecting deadline cleanup keeps original token",
                  ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, expired_generation, expired_wake_seq, 81U,
                      71U, COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK,
                      30505U));

    companion_controller_model_init(&model, 600U);
    expect_result("network test startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 601U));
    companion_controller_model_set_network(&model, false, 602U);
    expect_int("idle survives network loss", COMPANION_PRODUCT_IDLE,
               model.product_state);
    expect_true("network loss preserves requested roam", model.roam_enabled);
    expect_true("network loss only closes network gate",
                !model.network_ready && !model.upload_gate_open);
    companion_controller_model_set_network(&model, true, 603U);
    expect_int("network recovery keeps idle state", COMPANION_PRODUCT_IDLE,
               model.product_state);
    expect_true("network recovery preserves requested roam",
                model.roam_enabled);
    expect_result("network test reserves wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 604U, &generation, &wake_seq));
    companion_controller_model_set_network(&model, false, 605U);
    expect_int("network loss cancels reserved wake", COMPANION_PRODUCT_IDLE,
               model.product_state);
    expect_true("network loss clears reservation", !model.wake_reserved);

    expect_result("agent error enters recoverable error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AGENT, false,
                      606U));
    expect_result("matching restored core recovers idle", ESP_OK,
                  companion_controller_model_recover_error(
                      &model, COMPANION_CONTROLLER_ERROR_AGENT,
                      true, 607U));
    expect_result("audio restart-required error enters error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AUDIO,
                      true, 608U));
    expect_result("restart-required error cannot auto recover",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_recover_error(
                      &model, COMPANION_CONTROLLER_ERROR_AUDIO,
                      true, 609U));

    companion_controller_model_init(&model, 608U);
    expect_result("error escalation startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 609U));
    expect_result("error escalation enters recoverable agent error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AGENT,
                      false, 610U));
    expect_result("audio fatal escalates existing agent error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AUDIO,
                      true, 611U));
    expect_int("audio fatal owns escalated error reason",
               COMPANION_CONTROLLER_ERROR_AUDIO, model.error_reason);
    expect_true("audio fatal latches restart-required after escalation",
                model.restart_required);
    expect_result("agent recovery cannot clear escalated audio error",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_recover_error(
                      &model, COMPANION_CONTROLLER_ERROR_AGENT,
                      true, 612U));

    expect_true("roam starts enabled before SW3 click",
                model.roam_enabled);
    expect_true("first SW3 click disables roam",
                !companion_controller_model_toggle_roam(&model));
    expect_true("second SW3 click enables roam",
                companion_controller_model_toggle_roam(&model));
}

static void run_controller_capability_tests(void)
{
    companion_controller_model_t model = {0};
    companion_controller_output_t output = {0};
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_init(&model, 0U);

    companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_COUNT,
    };
    expect_result("unknown controller input is rejected",
                  ESP_ERR_INVALID_ARG,
                  test_model_apply(&model, &input, &output));
    companion_controller_model_snapshot(&model, &snapshot);
    expect_int("invalid input leaves controller booting",
               COMPANION_PRODUCT_BOOTING, snapshot.product_state);

    input = (companion_controller_input_t){
        .type = COMPANION_CONTROLLER_INPUT_CAPABILITY_SNAPSHOT,
        .now_ms = 1U,
        .data.capability = {
            .capability = COMPANION_CAPABILITY_AUDIO,
            .available = true,
            .error = ESP_OK,
            .revision = 1U,
        },
    };
    expect_result("audio capability revision is accepted", ESP_OK,
                  test_model_apply(&model, &input, &output));
    input.now_ms = 2U;
    input.data.capability.capability = COMPANION_CAPABILITY_AGENT;
    expect_result("agent capability revision is accepted", ESP_OK,
                  test_model_apply(&model, &input, &output));
    expect_result("capability-ready startup enters idle", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 3U));

    input.now_ms = 4U;
    input.data.capability.available = false;
    input.data.capability.error = ESP_FAIL;
    input.data.capability.revision = 2U;
    expect_result("agent loss enters bounded error", ESP_OK,
                  test_model_apply(&model, &input, &output));
    companion_controller_model_snapshot(&model, &snapshot);
    expect_int("agent loss owns error reason",
               COMPANION_CONTROLLER_ERROR_AGENT, snapshot.error_reason);
    expect_true("agent loss allows controlled recovery",
                !snapshot.restart_required);

    input.data.capability.revision = 1U;
    expect_result("stale capability revision is rejected",
                  ESP_ERR_INVALID_STATE,
                  test_model_apply(&model, &input, &output));
    input.now_ms = 5U;
    input.data.capability.available = true;
    input.data.capability.error = ESP_OK;
    input.data.capability.revision = 3U;
    expect_result("agent recovery revision returns to idle", ESP_OK,
                  test_model_apply(&model, &input, &output));
    companion_controller_model_snapshot(&model, &snapshot);
    expect_int("agent recovery establishes new idle epoch",
               COMPANION_PRODUCT_IDLE, snapshot.product_state);

    input.now_ms = 6U;
    input.data.capability.capability = COMPANION_CAPABILITY_AUDIO;
    input.data.capability.available = false;
    input.data.capability.error = ESP_FAIL;
    input.data.capability.revision = 2U;
    expect_result("audio loss enters restart-required error", ESP_OK,
                  test_model_apply(&model, &input, &output));
    input.now_ms = 7U;
    input.data.capability.available = true;
    input.data.capability.error = ESP_OK;
    input.data.capability.revision = 3U;
    expect_result("audio up revision is recorded", ESP_OK,
                  test_model_apply(&model, &input, &output));
    companion_controller_model_snapshot(&model, &snapshot);
    expect_int("audio fatal does not auto-recover",
               COMPANION_PRODUCT_ERROR, snapshot.product_state);
    expect_true("audio fatal remains restart-required",
                snapshot.restart_required);
}

static void run_controller_interrupt_tests(void)
{
    companion_controller_model_t model = {0};
    companion_controller_decision_t decision = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;

    companion_controller_model_init(&model, 700U);
    expect_result("interrupt startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 701U));
    expect_result("interrupt setup reserves wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 702U, &generation, &wake_seq));
    uint32_t duplicate_generation = 0U;
    uint32_t duplicate_wake_seq = 0U;
    expect_result("duplicate pending wake is rejected", ESP_ERR_INVALID_STATE,
                  companion_controller_model_reserve_wake(
                      &model, 702U, &duplicate_generation,
                      &duplicate_wake_seq));
    expect_result("interrupt setup skips doa", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, false, 0.0f, &decision));
    expect_result("interrupt setup accepts agent", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 81U, 703U));
    expect_result("interrupt setup enters listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 81U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 704U));
    expect_result("interrupt setup enters processing", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 81U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 705U));

    expect_result("processing rejects a new wake", ESP_ERR_INVALID_STATE,
                  companion_controller_model_reserve_wake(
                      &model, 706U, &generation, &wake_seq));
    expect_true("processing rejection preserves wake token",
                generation == model.generation && wake_seq == model.wake_seq);
    expect_int("processing rejection preserves state",
               COMPANION_PRODUCT_PROCESSING, model.product_state);
    expect_result("current request may move to speaking", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 81U,
                      COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK, 712U));

    const uint32_t speaking_generation = generation;
    const uint32_t speaking_wake_seq = wake_seq;
    expect_result("speaking accepts another wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 713U, &generation, &wake_seq));
    expect_true("speaking re-wake clears absolute deadline",
                0U == model.speaking_absolute_deadline_ms);
    expect_true("speaking re-wake clears TTS activity throttle",
                !model.tts_activity_seen &&
                0U == model.tts_last_activity_ms);
    expect_true("speaking re-wake requires localization",
                model.wake_requires_localization);
    expect_result("speaking re-wake accepts new request first",
                  ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 82U, 714U));
    expect_result("speaking re-wake enters locating",
                  ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 715U));
    expect_result("speaking re-wake center DOA closes only motion",
                  ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, 0.0f, &decision));
    expect_int("speaking re-wake center never requests agent",
               COMPANION_CONTROLLER_DECISION_NONE, decision.type);
    expect_result("old speaking stop is rejected", ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, speaking_generation, speaking_wake_seq,
                      91U, 81U, COMPANION_AGENT_SEMANTIC_CLOSED,
                      ESP_OK, 715U));
    expect_result("new interrupted request reaches listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 82U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 716U));

    expect_result("listening rejects a new wake", ESP_ERR_INVALID_STATE,
                  companion_controller_model_reserve_wake(
                      &model, 717U, &generation, &wake_seq));
    expect_true("listening rejection preserves wake token",
                generation == model.generation && wake_seq == model.wake_seq);
    expect_int("listening rejection preserves state",
               COMPANION_PRODUCT_LISTENING, model.product_state);
    expect_result("current listening request closes normally", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 82U,
                      COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK, 718U));
    expect_result("idle accepts next wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 719U, &generation, &wake_seq));
    expect_result("connecting setup accepts new request", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 83U, 720U));
    expect_result("connecting rejects a newer wake", ESP_ERR_INVALID_STATE,
                  companion_controller_model_reserve_wake(
                      &model, 721U, &generation, &wake_seq));
    expect_true("connecting rejection preserves wake token",
                generation == model.generation && wake_seq == model.wake_seq);
    expect_int("connecting rejection preserves state",
               COMPANION_PRODUCT_CONNECTING, model.product_state);
}

static void run_controller_parallel_plane_tests(void)
{
    companion_controller_model_t model = {0};
    companion_controller_decision_t decision = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;

    expect_true("idle Motion is not active",
                !companion_controller_motion_state_is_active(
                    COMPANION_CONTROLLER_MOTION_IDLE));
    expect_true("locating Motion is active",
                companion_controller_motion_state_is_active(
                    COMPANION_CONTROLLER_MOTION_LOCATING));
    expect_true("turning Motion is active",
                companion_controller_motion_state_is_active(
                    COMPANION_CONTROLLER_MOTION_TURNING));
    expect_true("stopping Motion is active",
                companion_controller_motion_state_is_active(
                    COMPANION_CONTROLLER_MOTION_STOPPING));
    expect_true("completed Motion is not active",
                !companion_controller_motion_state_is_active(
                    COMPANION_CONTROLLER_MOTION_COMPLETE));

    companion_controller_model_init(&model, 800U);
    expect_result("parallel setup startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 801U));
    expect_result("parallel wake reserves both planes", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 802U, &generation, &wake_seq));
    expect_result("parallel motion accepts locating", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 803U));
    expect_result("parallel session accepts before DOA completes", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 91U, 804U));
    expect_result("parallel session reaches ready while locating", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 101U, 91U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK,
                      805U));
    expect_int("parallel ready owns session state",
               COMPANION_PRODUCT_LISTENING, model.product_state);
    expect_result("parallel DOA completion remains accepted after ready",
                  ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, 30.0f,
                      &decision));
    expect_int("parallel DOA still creates turn effect",
               COMPANION_CONTROLLER_DECISION_START_TURN, decision.type);
    expect_result("parallel turn acceptance remains independent",
                  ESP_OK,
                  companion_controller_model_mark_motion_started(
                      &model, generation, wake_seq, 806U));
    expect_result("parallel motion completion keeps session closed loop",
                  ESP_OK,
                  companion_controller_model_on_motion_done(
                      &model, generation, wake_seq, &decision));
    expect_int("parallel motion completion does not notify agent",
               COMPANION_CONTROLLER_DECISION_NONE, decision.type);

    companion_controller_model_init(&model, 900U);
    expect_result("session failure setup startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 901U));
    expect_result("session failure setup wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 902U, &generation, &wake_seq));
    expect_result("session failure setup motion locates", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 903U));
    expect_result("session failure setup Agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 92U, 904U));
    expect_result("session failure closes only session plane", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 102U, 92U,
                      COMPANION_AGENT_SEMANTIC_FAILED, ESP_FAIL, 905U));
    expect_int("session failure preserves wake generation",
               (int)generation, (int)model.generation);
    expect_int("session failure preserves locating motion",
               COMPANION_CONTROLLER_MOTION_LOCATING, model.motion_state);
    expect_result("DOA remains accepted after session failure", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, -30.0f,
                      &decision));

    companion_controller_model_init(&model, 910U);
    expect_result("motion-only fatal setup startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 911U));
    expect_result("motion-only fatal setup wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 912U, &generation, &wake_seq));
    expect_result("motion-only fatal setup locates", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 913U));
    expect_result("motion-only fatal setup accepts Agent", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 95U, 914U));
    expect_result("motion-only fatal closes Agent plane first", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 105U, 95U,
                      COMPANION_AGENT_SEMANTIC_FAILED, ESP_FAIL, 915U));
    expect_int("motion-only fatal keeps locating before network loss",
               COMPANION_CONTROLLER_MOTION_LOCATING, model.motion_state);
    companion_controller_model_set_network(&model, false, 916U);
    expect_int("network fatal clears motion after Agent failure",
               COMPANION_CONTROLLER_MOTION_IDLE, model.motion_state);

    companion_controller_model_init(&model, 925U);
    expect_result("agent-first failure setup startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 926U));
    expect_result("agent-first failure setup wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 927U, &generation, &wake_seq));
    expect_result("agent-first failure accepts session before motion",
                  ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 94U, 928U));
    expect_result("agent-first failure closes only session plane", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 104U, 94U,
                      COMPANION_AGENT_SEMANTIC_FAILED, ESP_FAIL, 929U));
    expect_int("agent-first failure preserves wake generation",
               (int)generation, (int)model.generation);
    expect_true("agent-first failure preserves pending localization",
                model.wake_requires_localization);
    expect_result("motion can start after agent-first failure", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 930U));
    expect_int("motion start after Agent failure keeps session idle",
               COMPANION_PRODUCT_IDLE, model.product_state);

    companion_controller_model_init(&model, 932U);
    expect_result("sync Agent failure setup startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 933U));
    expect_result("sync Agent failure setup wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 934U, &generation, &wake_seq));
    expect_result("sync Agent failure closes only Agent plane", ESP_OK,
                  companion_controller_model_finish_agent(
                      &model, generation, wake_seq, 935U));
    expect_result("sync Agent failure still starts locating", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 936U));
    expect_result("sync Agent failure still plans Motion", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, 30.0f,
                      &decision));
    expect_result("sync Agent failure still accepts Motion", ESP_OK,
                  companion_controller_model_mark_motion_started(
                      &model, generation, wake_seq, 937U));
    expect_result("sync Agent failure Motion completes independently",
                  ESP_OK,
                  companion_controller_model_on_motion_done(
                      &model, generation, wake_seq, &decision));
    expect_int("Motion completion does not retry finished Agent",
               COMPANION_CONTROLLER_DECISION_NONE, decision.type);

    companion_controller_model_init(&model, 950U);
    expect_result("fatal cancel setup startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 951U));
    expect_result("fatal cancel setup wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 952U, &generation, &wake_seq));
    expect_result("fatal cancel setup motion locates", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 953U));
    expect_result("fatal cancel setup Agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 93U, 954U));
    companion_controller_model_set_network(&model, false, 955U);
    expect_int("network fatal returns session to idle",
               COMPANION_PRODUCT_IDLE, model.product_state);
    expect_int("network fatal clears motion plane",
               COMPANION_CONTROLLER_MOTION_IDLE, model.motion_state);
    expect_true("network fatal clears wake reservation",
                !model.wake_reserved);

    companion_controller_model_init(&model, 1000U);
    expect_result("closed motion setup startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 1001U));
    expect_result("closed motion setup wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 1002U, &generation, &wake_seq));
    expect_result("closed motion setup locates", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 1003U));
    expect_result("closed motion setup plans turn", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, 30.0f,
                      &decision));
    expect_int("closed motion setup turn decision",
               COMPANION_CONTROLLER_DECISION_START_TURN, decision.type);
    expect_result("closed motion setup accepts turn", ESP_OK,
                  companion_controller_model_mark_motion_started(
                      &model, generation, wake_seq, 1004U));
    expect_result("closed motion setup accepts agent", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 93U, 1005U));
    expect_result("closed motion session closes only session", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 103U, 93U,
                      COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK, 1006U));
    expect_int("closed motion preserves turning state",
               COMPANION_CONTROLLER_MOTION_TURNING, model.motion_state);
    expect_result("motion completes after session close", ESP_OK,
                  companion_controller_model_on_motion_done(
                      &model, generation, wake_seq, &decision));
    expect_int("motion completion does not restart closed agent",
               COMPANION_CONTROLLER_DECISION_NONE, decision.type);
}

static void run_controller_deadline_matrix_tests(void)
{
    companion_controller_model_t model = {0};
    companion_controller_decision_t decision = {0};
    companion_controller_deadline_t expired = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;

    companion_controller_model_init(&model, 0U);
    expect_result("locating deadline setup startup", ESP_OK,
                  companion_controller_model_finish_startup(&model, true, 1U));
    expect_result("locating deadline setup wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 2U, &generation, &wake_seq));
    expect_result("locating deadline setup accepted", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 3U));
    expect_true("locating deadline is delegated to controller fallback",
                companion_controller_model_poll_deadline(
                    &model, 2003U, &expired));
    expect_int("locating deadline keeps idle session independent",
               COMPANION_PRODUCT_IDLE, model.product_state);

    companion_controller_model_init(&model, 3000U);
    expect_result("parallel deadline setup startup", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 3001U));
    expect_result("parallel deadline setup wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 3002U, &generation, &wake_seq));
    expect_result("parallel deadline setup locates", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 3003U));
    expect_result("parallel deadline setup accepts agent", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 74U, 3004U));
    expect_result("parallel deadline setup ready", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 204U, 74U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK,
                      3005U));
    expect_true("motion deadline expires independently after ready",
                companion_controller_model_poll_deadline(
                    &model, 5003U, &expired));
    expect_true("motion deadline is identified as motion plane",
                expired.motion_plane);
    expect_int("motion deadline preserves session state",
               COMPANION_PRODUCT_LISTENING, model.product_state);
    expect_int("motion deadline preserves locating state",
               COMPANION_CONTROLLER_MOTION_LOCATING, model.motion_state);
    expect_result("session deadline cleanup preserves motion token", ESP_OK,
                  companion_controller_model_finish_agent(
                      &model, generation, wake_seq, 5004U));
    expect_int("session cleanup keeps locating motion",
               COMPANION_CONTROLLER_MOTION_LOCATING, model.motion_state);

    companion_controller_model_init(&model, 10U);
    expect_result("turning deadline setup startup", ESP_OK,
                  companion_controller_model_finish_startup(&model, true, 11U));
    expect_result("turning deadline setup wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 12U, &generation, &wake_seq));
    expect_result("turning deadline setup locating", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 13U));
    expect_result("turning deadline setup plan", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, 30.0f, &decision));
    expect_result("turning deadline setup accepted", ESP_OK,
                  companion_controller_model_mark_motion_started(
                      &model, generation, wake_seq, 14U));
    expect_true("turning remains active before 14 second deadline",
                !companion_controller_model_poll_deadline(
                    &model, 14013U, &expired));
    expect_true("turning deadline is delegated to controller fallback",
                companion_controller_model_poll_deadline(
                    &model, 14014U, &expired));
    expect_int("turning deadline keeps idle session independent",
               COMPANION_PRODUCT_IDLE, model.product_state);

    const companion_product_state_t deadline_states[] = {
        COMPANION_PRODUCT_LISTENING,
        COMPANION_PRODUCT_PROCESSING,
        COMPANION_PRODUCT_SPEAKING,
    };
    const uint64_t deadline_durations[] = {20000U, 17000U, 15000U};
    for (size_t index = 0U;
         index < sizeof(deadline_states) / sizeof(deadline_states[0]);
         ++index) {
        const uint64_t base = 100U + (uint64_t)index * 100U;
        companion_controller_model_init(&model, base);
        expect_result("agent deadline setup startup", ESP_OK,
                      companion_controller_model_finish_startup(
                          &model, true, base + 1U));
        expect_result("agent deadline setup wake", ESP_OK,
                      companion_controller_model_reserve_wake(
                          &model, base + 2U, &generation, &wake_seq));
        expect_result("agent deadline setup skip doa", ESP_OK,
                      companion_controller_model_on_doa(
                          &model, generation, wake_seq, false, 0.0f,
                          &decision));
        expect_result("agent deadline setup accepted", ESP_OK,
                      companion_controller_model_mark_agent_accepted(
                          &model, generation, wake_seq, 100U + (uint32_t)index,
                          base + 3U));
        expect_result("agent deadline setup listening", ESP_OK,
                      companion_controller_model_on_agent_semantic(
                          &model, generation, wake_seq, 200U + (uint32_t)index,
                          100U + (uint32_t)index,
                          COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK,
                          base + 4U));
        if (COMPANION_PRODUCT_PROCESSING == deadline_states[index] ||
            COMPANION_PRODUCT_SPEAKING == deadline_states[index]) {
            expect_result("agent deadline setup processing", ESP_OK,
                          companion_controller_model_on_agent_semantic(
                              &model, generation, wake_seq,
                              200U + (uint32_t)index,
                              100U + (uint32_t)index,
                              COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK,
                              base + 5U));
        }
        uint64_t entered_ms = (COMPANION_PRODUCT_LISTENING ==
                               deadline_states[index]) ? base + 4U :
                              base + 5U;
        if (COMPANION_PRODUCT_SPEAKING == deadline_states[index]) {
            expect_result("agent deadline setup speaking", ESP_OK,
                          companion_controller_model_on_agent_semantic(
                              &model, generation, wake_seq,
                              200U + (uint32_t)index,
                              100U + (uint32_t)index,
                              COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK,
                              base + 6U));
            entered_ms = base + 6U;
        }
        expect_true("agent activity deadline expires",
                    companion_controller_model_poll_deadline(
                        &model, entered_ms + deadline_durations[index],
                        &expired));
        expect_int("agent activity deadline stays owned until controller cleanup",
                   deadline_states[index], model.product_state);
        const uint32_t expired_generation = expired.generation;
        const uint32_t expired_wake_seq = expired.wake_seq;
        const bool speaking =
            COMPANION_PRODUCT_SPEAKING == deadline_states[index];
        expect_result(
            speaking ? "speaking deadline still permits wake replacement" :
                       "non-speaking deadline does not permit wake replacement",
            speaking ? ESP_OK : ESP_ERR_INVALID_STATE,
            companion_controller_model_reserve_wake(
                &model, entered_ms + deadline_durations[index] + 1U,
                &generation, &wake_seq));
        expect_true(
            speaking ? "speaking deadline token is replaced" :
                       "non-speaking deadline preserves original token",
            speaking ?
                (expired_generation != generation ||
                 expired_wake_seq != wake_seq) :
                (expired_generation == generation &&
                 expired_wake_seq == wake_seq));
        expect_result(
            speaking ? "replaced deadline token cannot close new request" :
                       "preserved deadline token performs cleanup",
            speaking ? ESP_ERR_INVALID_STATE : ESP_OK,
            companion_controller_model_on_agent_semantic(
                &model, expired_generation, expired_wake_seq,
                200U + (uint32_t)index,
                100U + (uint32_t)index,
                COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK,
                entered_ms + deadline_durations[index] + 2U));
    }

    companion_controller_model_init(&model, 500U);
    expect_result("error network test startup", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 501U));
    expect_result("error network test enters restart-required error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AUDIO, true, 502U));
    companion_controller_model_set_network(&model, false, 503U);
    expect_int("network loss does not overwrite error state",
               COMPANION_PRODUCT_ERROR, model.product_state);
    expect_int("network loss keeps original error reason",
               COMPANION_CONTROLLER_ERROR_AUDIO, model.error_reason);
    expect_true("network loss keeps restart-required latch",
                model.restart_required);
}

static void run_controller_speaking_activity_tests(void)
{
    companion_controller_model_t model = {0};
    companion_controller_decision_t decision = {0};
    companion_controller_deadline_t expired = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;

    companion_controller_model_init(&model, 0U);
    expect_result("speaking activity setup startup", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 1U));
    expect_result("speaking activity setup wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 2U, &generation, &wake_seq));
    expect_result("speaking activity setup skips doa", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, false, 0.0f,
                      &decision));
    expect_result("speaking activity setup accepts agent", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 71U, 3U));
    expect_result("speaking activity setup listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 81U, 71U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK,
                      4U));
    expect_result("speaking activity setup processing", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 81U, 71U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 5U));
    expect_result("speaking activity setup speaking", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 81U, 71U,
                      COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK, 6U));
    companion_controller_model_t absolute_model = model;
    companion_controller_model_t token_model = model;
    companion_controller_model_t regressed_model = model;
    companion_controller_model_t duplicate_model = model;

    expect_result("duplicate SPEAKING remains idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &duplicate_model, generation, wake_seq, 81U, 71U,
                      COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK, 1000U));
    expect_true("duplicate SPEAKING preserves inactivity deadline",
                15006U == duplicate_model.state_deadline_ms);
    expect_true("duplicate SPEAKING preserves absolute deadline",
                300006U ==
                duplicate_model.speaking_absolute_deadline_ms);
    expect_true("duplicate SPEAKING does not invent TTS activity",
                !duplicate_model.tts_activity_seen);

    expect_result("TTS activity rejects stale generation",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_tts_activity(
                      &token_model, generation + 1U, wake_seq,
                      81U, 71U, 1000U));
    expect_result("TTS activity rejects stale wake sequence",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_tts_activity(
                      &token_model, generation, wake_seq + 1U,
                      81U, 71U, 1000U));
    expect_result("TTS activity rejects stale session epoch",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_tts_activity(
                      &token_model, generation, wake_seq,
                      82U, 71U, 1000U));
    expect_result("TTS activity rejects stale request",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_tts_activity(
                      &token_model, generation, wake_seq,
                      81U, 72U, 1000U));
    expect_true("stale TTS activity does not change deadline",
                15006U == token_model.state_deadline_ms);
    expect_true("stale TTS activity does not arm throttle",
                !token_model.tts_activity_seen);
    expect_result("first TTS activity establishes monotonic timestamp",
                  ESP_OK,
                  companion_controller_model_on_tts_activity(
                      &regressed_model, generation, wake_seq,
                      81U, 71U, 1000U));
    expect_result("regressed TTS activity timestamp is rejected",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_tts_activity(
                      &regressed_model, generation, wake_seq,
                      81U, 71U, 999U));
    expect_true("regressed TTS activity preserves deadline",
                16000U == regressed_model.state_deadline_ms);
    expect_true("regressed TTS activity preserves throttle timestamp",
                1000U == regressed_model.tts_last_activity_ms);

    expect_result("current TTS activity before deadline is accepted", ESP_OK,
                  companion_controller_model_on_tts_activity(
                      &model, generation, wake_seq, 81U, 71U, 15005U));
    expect_true("TTS activity extends inactivity deadline",
                30005U == model.state_deadline_ms);
    companion_controller_model_t closed_model = model;
    companion_controller_model_t network_model = model;
    companion_controller_model_t fatal_model = model;
    expect_result("current CLOSED exits active speaking", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &closed_model, generation, wake_seq, 81U, 71U,
                      COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK, 15006U));
    expect_true("CLOSED clears speaking deadlines and throttle",
                0ULL == closed_model.state_deadline_ms &&
                0ULL == closed_model.speaking_absolute_deadline_ms &&
                !closed_model.tts_activity_seen &&
                0ULL == closed_model.tts_last_activity_ms);
    companion_controller_model_set_network(&network_model, false, 15006U);
    expect_true("network loss clears speaking deadlines and throttle",
                0ULL == network_model.state_deadline_ms &&
                0ULL == network_model.speaking_absolute_deadline_ms &&
                !network_model.tts_activity_seen &&
                0ULL == network_model.tts_last_activity_ms);
    expect_result("core fatal exits active speaking", ESP_OK,
                  companion_controller_model_enter_error(
                      &fatal_model, COMPANION_CONTROLLER_ERROR_AUDIO,
                      true, 15006U));
    expect_true("core fatal clears speaking deadlines and throttle",
                0ULL == fatal_model.state_deadline_ms &&
                0ULL == fatal_model.speaking_absolute_deadline_ms &&
                !fatal_model.tts_activity_seen &&
                0ULL == fatal_model.tts_last_activity_ms);
    expect_true("old inactivity deadline no longer expires",
                !companion_controller_model_poll_deadline(
                    &model, 15006U, &expired));
    expect_result("TTS activity inside one-second throttle is consumed",
                  ESP_OK,
                  companion_controller_model_on_tts_activity(
                      &model, generation, wake_seq, 81U, 71U, 16004U));
    expect_true("999ms TTS activity does not extend deadline",
                30005U == model.state_deadline_ms);
    expect_result("TTS activity at one-second boundary extends deadline",
                  ESP_OK,
                  companion_controller_model_on_tts_activity(
                      &model, generation, wake_seq, 81U, 71U, 16005U));
    expect_true("1000ms TTS activity extends deadline",
                31005U == model.state_deadline_ms);
    expect_result("TTS activity at inactivity deadline is rejected",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_tts_activity(
                      &model, generation, wake_seq, 81U, 71U, 31005U));
    expect_result("TTS activity after inactivity deadline is rejected",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_tts_activity(
                      &model, generation, wake_seq, 81U, 71U, 31006U));
    expect_true("speaking inactivity deadline expires",
                companion_controller_model_poll_deadline(
                    &model, 31005U, &expired));
    expect_int("speaking inactivity deadline reports inactivity reason",
               COMPANION_CONTROLLER_DEADLINE_SPEAKING_INACTIVITY,
               expired.reason);

    for (uint64_t activity_ms = 14006U;
         activity_ms < 300006U; activity_ms += 14000U) {
        expect_result("continuous TTS activity is accepted", ESP_OK,
                      companion_controller_model_on_tts_activity(
                          &absolute_model, generation, wake_seq,
                          81U, 71U, activity_ms));
        if (56006U == activity_ms) {
            expect_true("continuous TTS activity crosses old 60-second limit",
                        !companion_controller_model_poll_deadline(
                            &absolute_model, 60006U, &expired));
        }
    }
    expect_true("speaking absolute deadline expires at 300 seconds",
                companion_controller_model_poll_deadline(
                    &absolute_model, 300006U, &expired));
    expect_int("speaking absolute deadline reports hard-cap reason",
               COMPANION_CONTROLLER_DEADLINE_SPEAKING_ABSOLUTE,
               expired.reason);
}

static companion_touch_gesture_config_t touch_test_config(void)
{
    return (companion_touch_gesture_config_t){
        .display_width = 320U,
        .display_height = 240U,
        .press_debounce_ms = 30U,
        .touch_decision_ms = 150U,
        .release_debounce_ms = 120U,
        .tap_feedback_ms = 220U,
        .swipe_intent_horizontal_px = 12U,
        .swipe_min_horizontal_px = 30U,
        .swipe_max_vertical_px = 32U,
        .swipe_max_duration_ms = 700U,
    };
}

static void run_touch_gesture_tests(void)
{
    const companion_touch_gesture_config_t config = touch_test_config();
    companion_touch_gesture_t gesture = {0};
    companion_touch_gesture_result_t result = {0};

    expect_result("touch gesture config is valid", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    expect_result("12px intent starts contact", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 100U, 100U, 0U, &result));
    expect_result("12px intent is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 112U, 100U, 40U, &result));
    expect_int("12px intent does not switch pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);
    expect_result("29px swipe sample is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 129U, 100U, 100U, &result));
    expect_int("29px does not switch pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);

    expect_result("30px gesture reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 112U, 100U, 40U, &result);
    expect_result("30px swipe is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 130U, 100U, 100U, &result));
    expect_int("30px switches exactly one previous pack",
               COMPANION_GESTURE_PACK_PREVIOUS, result.pack_step);
    expect_result("continued swipe is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 170U, 100U, 110U, &result));
    expect_int("one contact cannot switch a second pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);

    expect_result("150ms touch reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 149U, &result);
    expect_int("149ms does not publish touch",
               COMPANION_TOUCH_TRANSITION_NONE, result.touch_transition);
    expect_result("150ms touch is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 100U, 100U, 150U, &result));
    expect_int("150ms publishes touch press",
               COMPANION_TOUCH_TRANSITION_PRESSED,
               result.touch_transition);
    expect_result("touch-active movement is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 150U, 100U, 160U, &result));
    expect_int("touch-active movement cannot become swipe",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);

    expect_result("700ms swipe reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 112U, 100U, 40U, &result);
    expect_result("700ms boundary swipe is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 130U, 100U, 700U, &result));
    expect_int("700ms boundary can switch pack",
               COMPANION_GESTURE_PACK_PREVIOUS, result.pack_step);

    expect_result("701ms swipe reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 112U, 100U, 40U, &result);
    expect_result("701ms late swipe is accepted as input", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 130U, 100U, 701U, &result));
    expect_int("701ms late swipe cannot switch pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);

    expect_result("quick tap reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 30U, &result);
    expect_result("quick tap release is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, false, 100U, 100U, 40U, &result));
    expect_int("quick tap publishes synthetic press",
               COMPANION_TOUCH_TRANSITION_PRESSED,
               result.touch_transition);
    expect_true("quick tap press is marked synthetic",
                result.synthetic_feedback);
    expect_result("quick tap feedback expiry is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, false, 100U, 100U, 260U, &result));
    expect_int("quick tap publishes synthetic release",
               COMPANION_TOUCH_TRANSITION_RELEASED,
               result.touch_transition);

    expect_result("quick tap recontact reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 30U, &result);
    (void)companion_touch_gesture_update(
        &gesture, false, 100U, 100U, 40U, &result);
    expect_result("new contact during quick tap is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 140U, 100U, 100U, &result));
    expect_int("new contact first releases synthetic feedback",
               COMPANION_TOUCH_TRANSITION_RELEASED,
               result.touch_transition);
    expect_int("new contact immediately enters debounce",
               COMPANION_GESTURE_CONTACT_DEBOUNCE,
               gesture.contact_state);
    expect_result("recontact reaches touch decision", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 140U, 100U, 250U, &result));
    expect_int("recontact publishes one normal press",
               COMPANION_TOUCH_TRANSITION_PRESSED,
               result.touch_transition);
    expect_true("recontact press is not synthetic",
                !result.synthetic_feedback);

    expect_result("diagonal touch reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    expect_result("diagonal movement is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 130U, 131U, 150U, &result));
    expect_int("diagonal movement does not switch pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);
    expect_int("diagonal movement resolves as touch",
               COMPANION_TOUCH_TRANSITION_PRESSED,
               result.touch_transition);
}

static void run_doa_estimator_tests(void)
{
    companion_doa_estimate_t estimate = {0};
    const float raw_40_left[] = {50.0f, 50.0f, 50.0f, 50.0f};
    expect_true("DOA raw 40 degree left consensus is valid",
                companion_doa_estimate(raw_40_left, 4U, &estimate));
    expect_true("DOA exposes raw relative angle for diagnostics",
                fabsf(estimate.raw_relative_deg - 40.0f) < 0.1f);
    expect_true("DOA maps raw 40 degrees to actual 90 degrees",
                fabsf(estimate.relative_deg - 90.0f) < 0.1f);

    const float raw_20_left[] = {70.0f, 70.0f, 70.0f, 70.0f};
    expect_true("DOA raw 20 degree left consensus is valid",
                companion_doa_estimate(raw_20_left, 4U, &estimate));
    expect_true("DOA maps raw 20 degrees to actual 45 degrees",
                fabsf(estimate.relative_deg - 45.0f) < 0.1f);
    companion_turn_plan_t turn_plan = {0};
    expect_result("actual DOA angle enters the public turn planner", ESP_OK,
                  companion_turn_plan_from_relative(estimate.relative_deg,
                                                    &turn_plan));
    expect_true("turn planner receives actual 45 degree target",
                fabsf(turn_plan.relative_deg - 45.0f) < 0.1f);

    const float raw_20_right[] = {110.0f, 110.0f, 110.0f, 110.0f};
    expect_true("DOA raw 20 degree right consensus is valid",
                companion_doa_estimate(raw_20_right, 4U, &estimate));
    expect_int("DOA right consensus direction", COMPANION_DOA_DIRECTION_RIGHT,
               estimate.direction);
    expect_true("DOA maps right direction to negative actual angle",
                fabsf(estimate.relative_deg + 45.0f) < 0.1f);

    const float raw_60_right[] = {150.0f, 150.0f, 150.0f, 150.0f};
    expect_true("DOA high right consensus is valid",
                companion_doa_estimate(raw_60_right, 4U, &estimate));
    expect_true("DOA actual angle is clamped at negative 90 degrees",
                fabsf(estimate.relative_deg + 90.0f) < 0.1f);

    const float actual_9_left[] = {86.0f, 86.0f, 86.0f, 86.0f};
    expect_true("DOA actual 9 degree sample is valid",
                companion_doa_estimate(actual_9_left, 4U, &estimate));
    expect_result("actual 9 degree target enters planner", ESP_OK,
                  companion_turn_plan_from_relative(estimate.relative_deg,
                                                    &turn_plan));
    expect_int("actual 9 degree target stays in dead zone",
               COMPANION_TURN_NONE, turn_plan.direction);

    const float actual_10_left[] = {85.55556f, 85.55556f,
                                    85.55556f, 85.55556f};
    expect_true("DOA actual 10 degree sample is valid",
                companion_doa_estimate(actual_10_left, 4U, &estimate));
    expect_result("actual 10 degree target enters planner", ESP_OK,
                  companion_turn_plan_from_relative(estimate.relative_deg,
                                                    &turn_plan));
    expect_int("actual 10 degree target starts a turn",
               COMPANION_TURN_LEFT, turn_plan.direction);

    const float center[] = {88.0f, 90.0f, 92.0f, 89.0f, 91.0f, 90.0f};
    expect_true("DOA center consensus is valid",
                companion_doa_estimate(center, 6U, &estimate));
    expect_true("DOA center does not turn", fabsf(estimate.relative_deg) < 0.1f);

    const float split[] = {30.0f, 35.0f, 40.0f, 130.0f, 135.0f, 140.0f};
    expect_true("DOA split directions are rejected",
                !companion_doa_estimate(split, 6U, &estimate));
    expect_true("DOA insufficient samples are rejected",
                !companion_doa_estimate(center, 3U, &estimate));

    const float scattered[] = {5.0f, 10.0f, 20.0f, 45.0f, 65.0f, 70.0f};
    expect_true("DOA dispersed direction is rejected",
                !companion_doa_estimate(scattered, 6U, &estimate));

    const float invalid[] = {0.0f, NAN, -2.0f, 181.0f, 40.0f, 42.0f,
                             44.0f, 46.0f, 48.0f, 50.0f};
    expect_true("DOA invalid samples are ignored",
                companion_doa_estimate(invalid, 10U, &estimate));
    expect_int("DOA invalid samples do not add votes", 6,
               estimate.sample_count);
}

static void run_motion_result_policy_tests(void)
{
    expect_int("Successful motion keeps the success class",
               COMPANION_MOTION_RESULT_SUCCESS,
               companion_motion_result_classify(
                   COMPANION_MOTION_FAILURE_NONE, false, ESP_OK, ESP_OK));
    expect_int("Cancellation takes precedence over failure details",
               COMPANION_MOTION_RESULT_CANCELLED,
               companion_motion_result_classify(
                   COMPANION_MOTION_FAILURE_FEEDBACK_STALL, true,
                   ESP_ERR_INVALID_STATE, ESP_OK));
    expect_int("IMU preparation failure remains retryable",
               COMPANION_MOTION_RESULT_RETRYABLE_SENSOR_FAILURE,
               companion_motion_result_classify(
                   COMPANION_MOTION_FAILURE_IMU_PREPARATION, false,
                   ESP_ERR_INVALID_STATE, ESP_OK));
    expect_int("IMU feedback timeout remains retryable",
               COMPANION_MOTION_RESULT_RETRYABLE_SENSOR_FAILURE,
               companion_motion_result_classify(
                   COMPANION_MOTION_FAILURE_FEEDBACK_TIMEOUT, false,
                   ESP_ERR_TIMEOUT, ESP_OK));
    expect_int("Wake turn stall is not reported as sensor failure",
               COMPANION_MOTION_RESULT_RETRYABLE_ACTUATION_STALL,
               companion_motion_result_classify(
                   COMPANION_MOTION_FAILURE_FEEDBACK_STALL, false,
                   ESP_ERR_TIMEOUT, ESP_OK));
    expect_int("Track output start failure is permanent",
               COMPANION_MOTION_RESULT_PERMANENT_OUTPUT_FAILURE,
               companion_motion_result_classify(
                   COMPANION_MOTION_FAILURE_OUTPUT_START, false,
                   ESP_FAIL, ESP_OK));
    expect_int("Safety stop failure is permanent",
               COMPANION_MOTION_RESULT_PERMANENT_OUTPUT_FAILURE,
               companion_motion_result_classify(
                   COMPANION_MOTION_FAILURE_STOP, false,
                   ESP_FAIL, ESP_FAIL));
}

static void run_imu_turn_control_tests(void)
{
    companion_turn_control_config_t config = {0};
    companion_turn_control_t control = {0};
    bool complete = false;

    companion_turn_control_config_default(&config);
    expect_true("IMU turn default stop lead is zero",
                fabsf(config.stop_lead_deg) < 0.001f);
    expect_int("IMU turn default hard timeout is 8000ms", 8000,
               (int)config.hard_timeout_ms);
    config.hard_timeout_ms = 1000U;
    const companion_imu_calibration_t z_axis_calibration = {
        .gyro_bias_raw = {0.0f, 0.0f, 100.0f},
        .gravity_unit = {0.0f, 0.0f, 1.0f},
    };
    expect_result("IMU turn accepts target and six-axis calibration", ESP_OK,
                  companion_turn_control_start(&control, &config, 30.0f,
                                               &z_axis_calibration));
    esp_err_t result = ESP_OK;
    for (int sample = 0; sample < 60 && !complete && ESP_OK == result;
         ++sample) {
        const uint32_t dt_us = (0 == (sample & 1)) ? 8000U : 12000U;
        const companion_imu_sample_t imu_sample = {
            .gyro_z_raw = 1740,
            .accel_z_raw = 8192,
            .accel_magnitude_raw = 8192.0f,
        };
        result = companion_turn_control_update_sample_us(
            &control, &config, &imu_sample, dt_us, &complete);
    }
    expect_result("IMU turn reaches closed-loop stop", ESP_OK, result);
    expect_true("IMU turn completes without early stop", complete);
    expect_true("IMU turn reaches full target angle",
                control.turned_deg >= 30.0f);
    expect_true("IMU turn reports zero actual remaining angle",
                companion_turn_control_remaining_deg(&control) < 0.001f);

    const companion_imu_sample_t stable[] = {
        {.gyro_x_raw = 10, .gyro_y_raw = -5, .gyro_z_raw = 100,
         .accel_z_raw = 8190, .accel_magnitude_raw = 8190.0f},
        {.gyro_x_raw = 15, .gyro_y_raw = 2, .gyro_z_raw = 105,
         .accel_z_raw = 8210, .accel_magnitude_raw = 8210.0f},
        {.gyro_x_raw = -8, .gyro_y_raw = 4, .gyro_z_raw = 98,
         .accel_z_raw = 8170, .accel_magnitude_raw = 8170.0f},
        {.gyro_x_raw = 7, .gyro_y_raw = -3, .gyro_z_raw = 103,
         .accel_z_raw = 8200, .accel_magnitude_raw = 8200.0f},
    };
    expect_true("stationary six-axis window is stable",
                companion_imu_samples_stable(stable, 4U, 82, 800.0f));
    companion_imu_sample_t gyro_moving[4] = {
        stable[0], stable[1], stable[2], stable[3]
    };
    gyro_moving[3].gyro_z_raw = 400;
    expect_true("gyro range over boundary is not stable",
                !companion_imu_samples_stable(gyro_moving, 4U, 82, 800.0f));
    companion_imu_sample_t accel_boundary[2] = {
        {.accel_z_raw = 8000, .accel_magnitude_raw = 8000.0f},
        {.accel_z_raw = 8800, .accel_magnitude_raw = 8800.0f},
    };
    expect_true("accel range at boundary is stable",
                companion_imu_samples_stable(accel_boundary, 2U, 82, 800.0f));
    accel_boundary[1].accel_magnitude_raw = 8800.1f;
    expect_true("accel range over boundary is not stable",
                !companion_imu_samples_stable(accel_boundary, 2U, 82, 800.0f));

    const companion_imu_sample_t calibration_samples[] = {
        {-2000, 195, 295, 0, 0, 8192, 8192.0f},
        {95, 198, 298, 0, 0, 8192, 8192.0f},
        {98, 199, 299, 0, 0, 8192, 8192.0f},
        {99, 200, 300, 0, 0, 8192, 8192.0f},
        {100, 200, 300, 0, 0, 8192, 8192.0f},
        {100, 200, 300, 0, 0, 8192, 8192.0f},
        {101, 201, 301, 0, 0, 8192, 8192.0f},
        {102, 202, 302, 0, 0, 8192, 8192.0f},
        {105, 205, 305, 0, 0, 8192, 8192.0f},
        {2500, 2200, 2300, 0, 0, 8192, 8192.0f},
    };
    companion_imu_calibration_t calibration = {0};
    expect_result("six-axis calibration rejects endpoint outliers", ESP_OK,
                  companion_imu_estimate_calibration(
                      calibration_samples, 10U, 1U, 20, &calibration));
    expect_true("three-axis gyro bias remains near stationary center",
                fabsf(calibration.gyro_bias_raw[0] - 100.0f) < 0.1f &&
                fabsf(calibration.gyro_bias_raw[1] - 200.625f) < 0.1f &&
                fabsf(calibration.gyro_bias_raw[2] - 300.625f) < 0.1f);
    expect_true("stationary acceleration defines the gravity unit axis",
                fabsf(calibration.gravity_unit[0]) < 0.001f &&
                fabsf(calibration.gravity_unit[1]) < 0.001f &&
                fabsf(calibration.gravity_unit[2] - 1.0f) < 0.001f);

    const float inv_sqrt_two = 0.70710678f;
    const companion_imu_calibration_t tilted_calibration = {
        .gyro_bias_raw = {100.0f, 200.0f, 300.0f},
        .gravity_unit = {inv_sqrt_two, 0.0f, inv_sqrt_two},
    };
    config.hard_timeout_ms = 1000U;
    expect_result("tilted IMU turn accepts gravity-axis calibration", ESP_OK,
                  companion_turn_control_start(&control, &config, 20.0f,
                                               &tilted_calibration));
    complete = false;
    result = ESP_OK;
    for (int sample = 0; sample < 40 && !complete && ESP_OK == result;
         ++sample) {
        const companion_imu_sample_t tilted_sample = {
            .gyro_x_raw = 1260,
            .gyro_y_raw = 200,
            .gyro_z_raw = 1460,
            .accel_x_raw = 5793,
            .accel_z_raw = 5793,
            .accel_magnitude_raw = 8192.0f,
        };
        result = companion_turn_control_update_sample_us(
            &control, &config, &tilted_sample, 10000U, &complete);
    }
    expect_result("tilted IMU reaches gravity-axis target", ESP_OK, result);
    expect_true("tilted IMU projection completes the turn", complete);
    expect_true("tilted IMU projected yaw is near 100 degrees per second",
                fabsf(fabsf(control.projected_rate_dps) - 100.0f) < 0.2f);

    expect_result("IMU stall setup succeeds", ESP_OK,
                  companion_turn_control_start(&control, &config, 45.0f,
                                               &z_axis_calibration));
    result = ESP_OK;
    for (int sample = 0; sample < 40 && ESP_OK == result; ++sample) {
        const companion_imu_sample_t stationary_sample = {
            .gyro_z_raw = 100,
            .accel_z_raw = 8192,
            .accel_magnitude_raw = 8192.0f,
        };
        result = companion_turn_control_update_sample_us(
            &control, &config, &stationary_sample, 10000U, &complete);
    }
    expect_result("IMU turn stops on stalled chassis", ESP_ERR_TIMEOUT,
                  result);
    expect_int("IMU stationary timeout identifies actuation stall",
               COMPANION_TURN_FAILURE_STALL, control.failure_reason);

    config.stall_timeout_ms = 1000U;
    config.hard_timeout_ms = 50U;
    expect_result("IMU hard-timeout setup succeeds", ESP_OK,
                  companion_turn_control_start(&control, &config, 45.0f,
                                               &z_axis_calibration));
    complete = false;
    result = ESP_OK;
    for (int sample = 0; sample < 5 && ESP_OK == result; ++sample) {
        const companion_imu_sample_t stationary_sample = {
            .gyro_z_raw = 100,
            .accel_z_raw = 8192,
            .accel_magnitude_raw = 8192.0f,
        };
        result = companion_turn_control_update_sample_us(
            &control, &config, &stationary_sample, 10000U, &complete);
    }
    expect_result("IMU turn stops at the hard safety timeout",
                  ESP_ERR_TIMEOUT, result);
    expect_int("IMU hard timeout is distinct from actuation stall",
               COMPANION_TURN_FAILURE_HARD_TIMEOUT,
               control.failure_reason);
}


int companion_core_run_tests(void)
{
    s_failure_count = 0;
    run_audio_voice_gate_tests();
    run_audio_vad_policy_tests();
    run_audio_pcm_queue_tests();
    run_audio_processor_policy_tests();
    run_audio_signal_metrics_tests();
    run_agent_vad_stop_policy_tests();
    run_tts_barrier_policy_tests();
    run_listen_mode_policy_tests();
    run_agent_binding_policy_tests();
    run_controller_runtime_policy_tests();
    run_controller_stop_policy_tests();
    run_controller_wake_effect_tests();
    run_prompt_vad_controller_tests();
    run_behavior_tests();
    run_expression_tests();
    run_controller_tests();
    run_controller_capability_tests();
    run_controller_interrupt_tests();
    run_controller_parallel_plane_tests();
    run_controller_deadline_matrix_tests();
    run_controller_speaking_activity_tests();
    run_touch_gesture_tests();
    run_doa_estimator_tests();
    run_motion_result_policy_tests();
    run_imu_turn_control_tests();
    run_ws_start_policy_tests();

    printf("companion_core_test: %s (%d failures)\n",
           (0 == s_failure_count) ? "PASS" : "FAIL", s_failure_count);
    return s_failure_count;
}
