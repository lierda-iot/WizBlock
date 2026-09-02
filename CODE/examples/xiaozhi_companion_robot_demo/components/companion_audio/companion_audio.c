#include "companion_audio.h"
#include "companion_audio_pcm_queue.h"
#include "companion_audio_processor_policy.h"
#include "companion_audio_vad_policy.h"
#include "companion_audio_voice_gate.h"

#include "audio_processor.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "esp_audio_simple_player_advance.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "io_expander.h"
#include "opus_codec.h"

#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#define OPUS_SAMPLE_RATE_HZ 16000U
#define OPUS_DECODE_SAMPLE_RATE_HZ 24000U
#define OPUS_FRAME_SAMPLES \
    (OPUS_SAMPLE_RATE_HZ * COMPANION_AUDIO_OPUS_FRAME_MS / 1000U)
#define OPUS_DECODE_FRAME_SAMPLES \
    (OPUS_DECODE_SAMPLE_RATE_HZ * COMPANION_AUDIO_OPUS_FRAME_MS / 1000U)
#define PLAYBACK_FRAME_SAMPLES \
    (COMPANION_AUDIO_SAMPLE_RATE_HZ * COMPANION_AUDIO_OPUS_FRAME_MS / 1000U)
#define PLAYBACK_TASK_STACK (40U * 1024U)
#define AUDIO_TASK_STACK (40U * 1024U)
_Static_assert(COMPANION_AUDIO_FETCH_TASK_PRIORITY >
                   COMPANION_AUDIO_FEED_TASK_PRIORITY,
               "WakeNet/VAD fetch must outrank capture feed");
_Static_assert(COMPANION_AUDIO_FEED_TASK_PRIORITY >
                   COMPANION_AUDIO_PLAYBACK_TASK_PRIORITY,
               "capture feed must outrank TTS playback");
_Static_assert(COMPANION_AUDIO_FEED_TASK_PRIORITY >
                   COMPANION_AUDIO_ENCODE_TASK_PRIORITY,
               "capture feed must outrank Opus encoding");
#define OPUS_BUFFER_BYTES 512U
#define AUDIO_ERROR_LOG_INTERVAL 50U
#define PROMPT_FADE_IN_SAMPLES 240
#define PROMPT_PREROLL_SAMPLES 240
#define AUDIO_TASK_STOP_TIMEOUT_MS 3000U
#define AUDIO_TASK_READY_TIMEOUT_MS 3000U
#define AUDIO_OUTPUT_STOP_TIMEOUT_MS 500U
#define AUDIO_REALTIME_LOG_INTERVAL_US 5000000LL
#define AUDIO_TTS_REALTIME_LOG_INTERVAL_US 1000000LL
#define AUDIO_PLAYBACK_REALTIME_LOG_INTERVAL_US 1000000LL
#define COMPANION_AUDIO_REF_SLOT 1U
#define COMPANION_AUDIO_REF_GAIN 4
#define COMPANION_AUDIO_AFE_CHANNELS 3U
#define AUDIO_SIGNAL_CORRELATION_LAGS 6U
#define AUDIO_SIGNAL_CORRELATION_DECIMATION 8U
#define AUDIO_SIGNAL_CORRELATION_HISTORY 257U
#define AUDIO_SIGNAL_PROBE_POINTS 16U
#define AUDIO_SIGNAL_PROBE_STRIDE 1024U
#define AUDIO_PLAY_READY_BIT BIT0
#define AUDIO_FEED_READY_BIT BIT1
#define AUDIO_FETCH_READY_BIT BIT2
#define AUDIO_PLAY_EXIT_BIT BIT3
#define AUDIO_FEED_EXIT_BIT BIT4
#define AUDIO_FETCH_EXIT_BIT BIT5
#define AUDIO_ENCODE_READY_BIT BIT6
#define AUDIO_ENCODE_EXIT_BIT BIT7
#define AUDIO_READY_BITS \
    (AUDIO_PLAY_READY_BIT | AUDIO_FEED_READY_BIT | AUDIO_FETCH_READY_BIT | \
     AUDIO_ENCODE_READY_BIT)
#define AUDIO_EXIT_BITS \
    (AUDIO_PLAY_EXIT_BIT | AUDIO_FEED_EXIT_BIT | AUDIO_FETCH_EXIT_BIT | \
     AUDIO_ENCODE_EXIT_BIT)

typedef enum {
    PROMPT_STATE_IDLE = 0,
    PROMPT_STATE_PLAYING,
} prompt_state_t;

typedef struct {
    uint8_t *data;
    int length;
    companion_audio_token_t token;
} playback_item_t;

typedef struct {
    uint64_t square_sum;
    uint32_t sample_count;
    uint32_t clipped_samples;
    int32_t peak;
} audio_level_diag_t;

typedef struct {
    bool valid;
    bool aec_enabled;
    companion_audio_output_owner_t owner;
    companion_audio_output_phase_t phase;
    companion_audio_token_t token;
} audio_output_diag_t;

typedef struct {
    int64_t sum_x;
    int64_t sum_y;
    uint64_t sum_x2;
    uint64_t sum_y2;
    int64_t sum_xy;
    uint32_t sample_count;
} audio_correlation_accumulator_t;

typedef struct {
    bool enabled;
    uint32_t sample_count;
    uint32_t late_blocks;
    size_t ref_history_index;
    int16_t ref_history[AUDIO_SIGNAL_CORRELATION_HISTORY];
    audio_correlation_accumulator_t mic1_ref[AUDIO_SIGNAL_CORRELATION_LAGS];
    audio_correlation_accumulator_t mic2_ref[AUDIO_SIGNAL_CORRELATION_LAGS];
    uint32_t probe_count;
    uint32_t next_probe_sample;
    int16_t mic1_probe[AUDIO_SIGNAL_PROBE_POINTS];
    int16_t mic2_probe[AUDIO_SIGNAL_PROBE_POINTS];
    int16_t ref_probe[AUDIO_SIGNAL_PROBE_POINTS];
} audio_signal_diag_t;

static const uint16_t s_audio_signal_correlation_lags[] = {
    0U, 16U, 32U, 64U, 128U, 256U,
};

static const char *TAG = "companion_audio";

static companion_audio_config_t s_config;
static esp_codec_dev_handle_t s_output_device;
static size_t s_feed_chunk;
static size_t s_fetch_chunk;
static volatile bool s_initialized;
static volatile bool s_running;
static bool s_starting;
static bool s_stop_requested;
static volatile bool s_was_vad_active;
static bool s_have_upload_token;
static companion_audio_token_t s_last_upload_token;
static QueueHandle_t s_playback_queue;
static TaskHandle_t s_playback_task;
static TaskHandle_t s_feed_task;
static TaskHandle_t s_fetch_task;
static TaskHandle_t s_encode_task;
static EventGroupHandle_t s_task_events;
static portMUX_TYPE s_lifecycle_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_fatal_reported;
static esp_err_t s_fatal_error;
/* Keep capture and window identities separate so a new wake cannot be
 * cleared by an older VAD window that is still draining. */
static companion_audio_vad_tracker_t s_vad_tracker;

static SemaphoreHandle_t s_snapshot_lock;
static int16_t *s_ring_mic1;
static int16_t *s_ring_mic2;
static int16_t *s_frozen_mic1;
static int16_t *s_frozen_mic2;
static size_t s_ring_write_index;
static size_t s_ring_count;
static uint32_t s_frozen_wake_seq;
static companion_audio_stats_t s_stats;
static SemaphoreHandle_t s_pcm_queue_lock;
static companion_audio_pcm_frame_t *s_pcm_storage;
static companion_audio_pcm_queue_t s_pcm_queue;

static esp_asp_handle_t s_prompt_player;
static SemaphoreHandle_t s_prompt_lock;
static SemaphoreHandle_t s_prompt_terminal;
static SemaphoreHandle_t s_output_lock;
static bool s_prompt_ready;
static volatile prompt_state_t s_prompt_state;
static int s_prompt_fade_remaining;
static companion_audio_output_owner_t s_output_owner;
static companion_audio_output_phase_t s_output_phase;
static companion_audio_token_t s_output_token;
static companion_audio_token_t s_prompt_callback_token;
static bool s_tts_aec_enabled;

static void drain_playback_queue(void);

static void audio_level_diag_add(audio_level_diag_t *diag, int32_t sample,
                                 bool clipped)
{
    if (NULL == diag) {
        return;
    }
    const int32_t magnitude = (0 > sample) ? -sample : sample;
    diag->square_sum += (uint64_t)((int64_t)sample * (int64_t)sample);
    diag->sample_count++;
    if (magnitude > diag->peak) {
        diag->peak = magnitude;
    }
    if (clipped) {
        diag->clipped_samples++;
    }
}

static double audio_level_diag_rms(const audio_level_diag_t *diag)
{
    if (NULL == diag || 0U == diag->sample_count) {
        return 0.0;
    }
    return sqrt((double)diag->square_sum / (double)diag->sample_count);
}

static void audio_correlation_accumulator_add(
    audio_correlation_accumulator_t *accumulator, int32_t x, int32_t y)
{
    if (NULL == accumulator) {
        return;
    }
    accumulator->sum_x += x;
    accumulator->sum_y += y;
    accumulator->sum_x2 += (uint64_t)((int64_t)x * (int64_t)x);
    accumulator->sum_y2 += (uint64_t)((int64_t)y * (int64_t)y);
    accumulator->sum_xy += (int64_t)x * (int64_t)y;
    accumulator->sample_count++;
}

static double audio_correlation_value(
    const audio_correlation_accumulator_t *accumulator)
{
    if (NULL == accumulator || 2U > accumulator->sample_count) {
        return 0.0;
    }
    const double count = (double)accumulator->sample_count;
    const double numerator = count * (double)accumulator->sum_xy -
                             (double)accumulator->sum_x *
                                 (double)accumulator->sum_y;
    const double x_energy = count * (double)accumulator->sum_x2 -
                            (double)accumulator->sum_x *
                                (double)accumulator->sum_x;
    const double y_energy = count * (double)accumulator->sum_y2 -
                            (double)accumulator->sum_y *
                                (double)accumulator->sum_y;
    if (0.0 >= x_energy || 0.0 >= y_energy) {
        return 0.0;
    }
    double value = numerator / sqrt(x_energy * y_energy);
    if (value > 1.0) {
        value = 1.0;
    } else if (value < -1.0) {
        value = -1.0;
    }
    return value;
}

static void audio_signal_diag_reset(audio_signal_diag_t *diag, bool enabled)
{
    if (NULL == diag) {
        return;
    }
    *diag = (audio_signal_diag_t){0};
    diag->enabled = enabled;
}

static void audio_signal_diag_add(audio_signal_diag_t *diag, int16_t mic1,
                                  int16_t mic2, int16_t ref_raw)
{
    if (NULL == diag || !diag->enabled) {
        return;
    }
    const uint32_t sample_index = diag->sample_count;
    if (diag->probe_count < AUDIO_SIGNAL_PROBE_POINTS &&
        sample_index >= diag->next_probe_sample) {
        diag->mic1_probe[diag->probe_count] = mic1;
        diag->mic2_probe[diag->probe_count] = mic2;
        diag->ref_probe[diag->probe_count] = ref_raw;
        diag->probe_count++;
        diag->next_probe_sample += AUDIO_SIGNAL_PROBE_STRIDE;
    }

    const size_t history_index = diag->ref_history_index;
    if (0U == (sample_index % AUDIO_SIGNAL_CORRELATION_DECIMATION)) {
        for (size_t lag_index = 0U;
             lag_index < AUDIO_SIGNAL_CORRELATION_LAGS; ++lag_index) {
            const uint16_t lag = s_audio_signal_correlation_lags[lag_index];
            if (0U != lag && sample_index < lag) {
                continue;
            }
            const int16_t delayed_ref =
                0U == lag ? ref_raw :
                            diag->ref_history[(history_index +
                                               AUDIO_SIGNAL_CORRELATION_HISTORY -
                                               lag) %
                                              AUDIO_SIGNAL_CORRELATION_HISTORY];
            audio_correlation_accumulator_add(&diag->mic1_ref[lag_index],
                                              mic1, delayed_ref);
            audio_correlation_accumulator_add(&diag->mic2_ref[lag_index],
                                              mic2, delayed_ref);
        }
    }
    diag->ref_history[history_index] = ref_raw;
    diag->ref_history_index =
        (history_index + 1U) % AUDIO_SIGNAL_CORRELATION_HISTORY;
    diag->sample_count++;
}

static void log_audio_signal_diag(const audio_signal_diag_t *diag,
                                  int64_t window_us, size_t tdm_frames)
{
    if (NULL == diag || !diag->enabled) {
        return;
    }
    const double expected_samples =
        (double)window_us * (double)OPUS_SAMPLE_RATE_HZ / 1000000.0;
    const double capture_ratio =
        0.0 < expected_samples ?
        (double)diag->sample_count / expected_samples : 0.0;
    const double block_period_us =
        1000000.0 * (double)tdm_frames / (double)OPUS_SAMPLE_RATE_HZ;
    ESP_LOGI(TAG,
             "[DEBUG-AI-SIGNAL] phase=feed window_ms=%.1f samples=%lu expected=%.0f capture_ratio=%.3f late_blocks=%lu block_ms=%.1f probe_count=%lu corr_lag_ms=0,1,2,4,8,16 mic1_ref=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f mic2_ref=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
             (double)window_us / 1000.0, (unsigned long)diag->sample_count,
             expected_samples, capture_ratio, (unsigned long)diag->late_blocks,
             block_period_us / 1000.0, (unsigned long)diag->probe_count,
             audio_correlation_value(&diag->mic1_ref[0]),
             audio_correlation_value(&diag->mic1_ref[1]),
             audio_correlation_value(&diag->mic1_ref[2]),
             audio_correlation_value(&diag->mic1_ref[3]),
             audio_correlation_value(&diag->mic1_ref[4]),
             audio_correlation_value(&diag->mic1_ref[5]),
             audio_correlation_value(&diag->mic2_ref[0]),
             audio_correlation_value(&diag->mic2_ref[1]),
             audio_correlation_value(&diag->mic2_ref[2]),
             audio_correlation_value(&diag->mic2_ref[3]),
             audio_correlation_value(&diag->mic2_ref[4]),
             audio_correlation_value(&diag->mic2_ref[5]));
    ESP_LOGI(TAG,
             "[DEBUG-AI-SIGNAL] raw_probe channel=mic1 count=%lu values=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
             (unsigned long)diag->probe_count, diag->mic1_probe[0],
             diag->mic1_probe[1], diag->mic1_probe[2], diag->mic1_probe[3],
             diag->mic1_probe[4], diag->mic1_probe[5], diag->mic1_probe[6],
             diag->mic1_probe[7], diag->mic1_probe[8], diag->mic1_probe[9],
             diag->mic1_probe[10], diag->mic1_probe[11],
             diag->mic1_probe[12], diag->mic1_probe[13],
             diag->mic1_probe[14], diag->mic1_probe[15]);
    ESP_LOGI(TAG,
             "[DEBUG-AI-SIGNAL] raw_probe channel=mic2 count=%lu values=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
             (unsigned long)diag->probe_count, diag->mic2_probe[0],
             diag->mic2_probe[1], diag->mic2_probe[2], diag->mic2_probe[3],
             diag->mic2_probe[4], diag->mic2_probe[5], diag->mic2_probe[6],
             diag->mic2_probe[7], diag->mic2_probe[8], diag->mic2_probe[9],
             diag->mic2_probe[10], diag->mic2_probe[11],
             diag->mic2_probe[12], diag->mic2_probe[13],
             diag->mic2_probe[14], diag->mic2_probe[15]);
    ESP_LOGI(TAG,
             "[DEBUG-AI-SIGNAL] raw_probe channel=ref_raw count=%lu values=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
             (unsigned long)diag->probe_count, diag->ref_probe[0],
             diag->ref_probe[1], diag->ref_probe[2], diag->ref_probe[3],
             diag->ref_probe[4], diag->ref_probe[5], diag->ref_probe[6],
             diag->ref_probe[7], diag->ref_probe[8], diag->ref_probe[9],
             diag->ref_probe[10], diag->ref_probe[11], diag->ref_probe[12],
             diag->ref_probe[13], diag->ref_probe[14], diag->ref_probe[15]);
}

static bool capture_output_diag(audio_output_diag_t *diag)
{
    if (NULL == diag) {
        return false;
    }
    *diag = (audio_output_diag_t){0};
    if (NULL == s_output_lock ||
        pdTRUE != xSemaphoreTake(s_output_lock, 0U)) {
        return false;
    }
    diag->valid = true;
    diag->aec_enabled = s_tts_aec_enabled;
    diag->owner = s_output_owner;
    diag->phase = s_output_phase;
    diag->token = s_output_token;
    xSemaphoreGive(s_output_lock);
    return true;
}

static int64_t audio_diag_interval_us(const audio_output_diag_t *diag)
{
    return NULL != diag && diag->valid && diag->aec_enabled ?
           AUDIO_TTS_REALTIME_LOG_INTERVAL_US :
           AUDIO_REALTIME_LOG_INTERVAL_US;
}

static void log_aec_state_locked(const char *event)
{
    ESP_LOGI(TAG,
             "[DEBUG-AI-AUDIO] event=%s aec=%u owner=%d output_phase=%d generation=%lu wake_seq=%lu session=%lu request=%lu timestamp_ms=%llu",
             event, s_tts_aec_enabled ? 1U : 0U, (int)s_output_owner,
             (int)s_output_phase, (unsigned long)s_output_token.generation,
             (unsigned long)s_output_token.wake_seq,
             (unsigned long)s_output_token.session_epoch,
             (unsigned long)s_output_token.request_id,
             (unsigned long long)(esp_timer_get_time() / 1000LL));
}

static esp_err_t set_tts_aec_enabled_locked(bool enabled)
{
    if (enabled == s_tts_aec_enabled) {
        return ESP_OK;
    }
    const esp_err_t result = enabled ? audio_processor_enable_aec() :
                                       audio_processor_disable_aec();
    if (ESP_OK == result) {
        s_tts_aec_enabled = enabled;
        ESP_LOGI(TAG, "TTS AEC %s", enabled ? "enabled" : "disabled");
    }
    return result;
}

static bool token_valid(const companion_audio_token_t *token)
{
    return NULL != token && 0U != token->generation &&
           0U != token->wake_seq && 0U != token->request_id;
}

static bool token_matches(const companion_audio_token_t *first,
                          const companion_audio_token_t *second)
{
    return token_valid(first) && token_valid(second) &&
           first->generation == second->generation &&
           first->wake_seq == second->wake_seq &&
           first->request_id == second->request_id &&
           (first->session_epoch == second->session_epoch ||
            0U == first->session_epoch || 0U == second->session_epoch);
}

static companion_audio_vad_prompt_token_t vad_prompt_token_from_audio(
    const companion_audio_token_t *token)
{
    if (NULL == token) {
        return (companion_audio_vad_prompt_token_t){0};
    }
    return (companion_audio_vad_prompt_token_t){
        .generation = token->generation,
        .wake_seq = token->wake_seq,
        .session_epoch = token->session_epoch,
        .request_id = token->request_id,
    };
}

static bool bind_vad_prompt_gate(const companion_audio_token_t *token)
{
    const companion_audio_vad_prompt_token_t prompt_token =
        vad_prompt_token_from_audio(token);
    bool bound = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    bound = companion_audio_vad_tracker_prompt_started(
        &s_vad_tracker, &prompt_token);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return bound;
}

static bool request_vad_prompt_reset(const companion_audio_token_t *token)
{
    const companion_audio_vad_prompt_token_t prompt_token =
        vad_prompt_token_from_audio(token);
    bool requested = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    requested = companion_audio_vad_tracker_prompt_terminal(
        &s_vad_tracker, &prompt_token);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return requested;
}

static bool bind_vad_upload_token(const companion_audio_token_t *token)
{
    const companion_audio_vad_prompt_token_t upload_token =
        vad_prompt_token_from_audio(token);
    bool bound = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    bound = companion_audio_vad_tracker_bind_upload_token(
        &s_vad_tracker, &upload_token);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return bound;
}

static void remember_upload_token(const companion_audio_token_t *token)
{
    if (NULL == token || !token_valid(token)) {
        return;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_last_upload_token = *token;
    s_have_upload_token = true;
    portEXIT_CRITICAL(&s_lifecycle_lock);
}

static bool retire_stale_upload_capture(
    const companion_audio_token_t *current_token,
    bool current_token_valid)
{
    companion_audio_token_t retired_token = {0};
    bool retired = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (!s_have_upload_token ||
        (current_token_valid &&
         token_matches(&s_last_upload_token, current_token))) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return false;
    }
    retired_token = s_last_upload_token;
    retired = companion_audio_vad_tracker_retire(
        &s_vad_tracker,
        retired_token.generation,
        retired_token.wake_seq,
        retired_token.session_epoch,
        retired_token.request_id);
    s_have_upload_token = false;
    s_last_upload_token = (companion_audio_token_t){0};
    s_was_vad_active = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    ESP_LOGI(TAG,
             "VAD tracker retired generation=%lu wake_seq=%lu session=%lu request=%lu retired=%u timestamp_ms=%llu",
             (unsigned long)retired_token.generation,
             (unsigned long)retired_token.wake_seq,
             (unsigned long)retired_token.session_epoch,
             (unsigned long)retired_token.request_id,
             retired ? 1U : 0U,
             (unsigned long long)(esp_timer_get_time() / 1000LL));
    return true;
}

static bool get_upload_token(companion_audio_token_t *token)
{
    if (NULL == token || NULL == s_config.get_upload_token) {
        return false;
    }
    *token = (companion_audio_token_t){0};
    return s_config.get_upload_token(token, s_config.user_ctx) &&
           token_valid(token);
}

static void clear_pcm_queue(void)
{
    if (NULL == s_pcm_queue_lock ||
        pdTRUE != xSemaphoreTake(s_pcm_queue_lock, pdMS_TO_TICKS(20U))) {
        return;
    }
    companion_audio_pcm_queue_clear(&s_pcm_queue);
    s_stats.pcm_queue_depth = 0U;
    xSemaphoreGive(s_pcm_queue_lock);
}

static esp_err_t enqueue_pcm_frame(
    const int16_t *samples, const companion_audio_token_t *token)
{
    if (NULL == s_pcm_queue_lock ||
        pdTRUE != xSemaphoreTake(s_pcm_queue_lock, 0U)) {
        s_stats.pcm_queue_drops++;
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = companion_audio_pcm_queue_push(
        &s_pcm_queue, samples, token);
    s_stats.pcm_queue_depth =
        (uint32_t)companion_audio_pcm_queue_count(&s_pcm_queue);
    s_stats.pcm_queue_high_water = (uint32_t)s_pcm_queue.high_water;
    if (ESP_OK != result) {
        s_stats.pcm_queue_drops++;
    }
    xSemaphoreGive(s_pcm_queue_lock);
    if (ESP_OK == result && NULL != s_encode_task) {
        (void)xTaskNotifyGive(s_encode_task);
    }
    return result;
}

static esp_err_t dequeue_pcm_frame(companion_audio_pcm_frame_t *frame)
{
    if (NULL == frame || NULL == s_pcm_queue_lock ||
        pdTRUE != xSemaphoreTake(s_pcm_queue_lock,
                                 pdMS_TO_TICKS(20U))) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = companion_audio_pcm_queue_pop(
        &s_pcm_queue, frame);
    s_stats.pcm_queue_depth =
        (uint32_t)companion_audio_pcm_queue_count(&s_pcm_queue);
    xSemaphoreGive(s_pcm_queue_lock);
    return result;
}

static bool output_identity_matches(companion_audio_output_owner_t owner,
                                    const companion_audio_token_t *token)
{
    return owner == s_output_owner && token_matches(token, &s_output_token);
}

static void rollback_tts_output(const companion_audio_token_t *token)
{
    bool owned = false;
    if (NULL != s_output_lock &&
        pdTRUE == xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(100))) {
        owned = output_identity_matches(COMPANION_AUDIO_OUTPUT_TTS, token);
        if (owned) {
            const esp_err_t aec_result =
                set_tts_aec_enabled_locked(false);
            if (ESP_OK != aec_result) {
                ESP_LOGE(TAG, "failed to disable TTS AEC during rollback: %s",
                         esp_err_to_name(aec_result));
            }
            log_aec_state_locked("tts_rollback_aec_off");
            s_output_owner = COMPANION_AUDIO_OUTPUT_SILENT;
            s_output_phase = COMPANION_AUDIO_OUTPUT_IDLE;
            s_output_token = (companion_audio_token_t){0};
        }
        xSemaphoreGive(s_output_lock);
    }
}

static bool audio_runtime_accepts_output(void)
{
    bool available = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    available = s_initialized && s_running && !s_starting &&
                !s_fatal_reported && NULL != s_playback_task &&
                NULL != s_feed_task && NULL != s_fetch_task &&
                NULL != s_encode_task;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return available;
}

static void drain_playback_queue(void)
{
    if (NULL == s_playback_queue) {
        return;
    }
    playback_item_t item = {0};
    while (pdTRUE == xQueueReceive(s_playback_queue, &item, 0U)) {
        heap_caps_free(item.data);
        item = (playback_item_t){0};
    }
}

static void mark_task_exit(EventBits_t exit_bit, TaskHandle_t *handle)
{
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (NULL != handle) {
        *handle = NULL;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (NULL != s_task_events) {
        (void)xEventGroupSetBits(s_task_events, exit_bit);
    }
}

static size_t resample_to_playback(const int16_t *source, size_t source_samples,
                                   int16_t *destination,
                                   size_t destination_capacity)
{
    if (NULL == source || NULL == destination || 0U == source_samples ||
        0U == destination_capacity) {
        return 0U;
    }
    size_t destination_samples =
        (source_samples * COMPANION_AUDIO_SAMPLE_RATE_HZ) /
        OPUS_DECODE_SAMPLE_RATE_HZ;
    if (destination_samples > destination_capacity) {
        destination_samples = destination_capacity;
    }
    for (size_t index = 0U; index < destination_samples; ++index) {
        const uint64_t source_q16 =
            (((uint64_t)index * OPUS_DECODE_SAMPLE_RATE_HZ) << 16) /
            COMPANION_AUDIO_SAMPLE_RATE_HZ;
        size_t source_index = (size_t)(source_q16 >> 16);
        uint32_t fraction = (uint32_t)(source_q16 & 0xFFFFU);
        if (source_index >= source_samples) {
            source_index = source_samples - 1U;
            fraction = 0U;
        }
        const int32_t first = source[source_index];
        const int32_t second = (source_index + 1U < source_samples) ?
                               source[source_index + 1U] : first;
        destination[index] = (int16_t)(first +
            (int32_t)(((int64_t)(second - first) * fraction) >> 16));
    }
    return destination_samples;
}

static void enable_amplifier(void)
{
    (void)io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                        BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    (void)io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
}

static void update_ring(const int16_t *interleaved, size_t frames)
{
    if (NULL == interleaved || NULL == s_snapshot_lock ||
        pdTRUE != xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(10))) {
        return;
    }
    for (size_t index = 0U; index < frames; ++index) {
        s_ring_mic1[s_ring_write_index] = interleaved[index * 2U];
        s_ring_mic2[s_ring_write_index] = interleaved[index * 2U + 1U];
        s_ring_write_index = (s_ring_write_index + 1U) %
                             COMPANION_AUDIO_SNAPSHOT_FRAMES;
        if (s_ring_count < COMPANION_AUDIO_SNAPSHOT_FRAMES) {
            s_ring_count++;
        }
    }
    xSemaphoreGive(s_snapshot_lock);
}

static esp_err_t freeze_snapshot(uint32_t wake_seq)
{
    if (0U == wake_seq || NULL == s_snapshot_lock ||
        pdTRUE != xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(20))) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_ring_count < COMPANION_AUDIO_SNAPSHOT_FRAMES) {
        xSemaphoreGive(s_snapshot_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const size_t oldest = s_ring_write_index;
    for (size_t index = 0U; index < COMPANION_AUDIO_SNAPSHOT_FRAMES; ++index) {
        const size_t source = (oldest + index) % COMPANION_AUDIO_SNAPSHOT_FRAMES;
        s_frozen_mic1[index] = s_ring_mic1[source];
        s_frozen_mic2[index] = s_ring_mic2[source];
    }
    s_frozen_wake_seq = wake_seq;
    s_stats.snapshot_version++;
    xSemaphoreGive(s_snapshot_lock);
    return ESP_OK;
}

static void emit_event(companion_audio_event_type_t type, uint32_t generation,
                       uint32_t wake_seq, esp_err_t result)
{
    if (NULL == s_config.on_event) {
        return;
    }
    const companion_audio_event_t event = {
        .type = type,
        .generation = generation,
        .wake_seq = wake_seq,
        .result = result,
    };
    s_config.on_event(&event, s_config.user_ctx);
}

static void stop_for_fatal_error(esp_err_t error)
{
    bool publish = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (!s_fatal_reported) {
        s_fatal_reported = true;
        s_fatal_error = error;
        publish = true;
    }
    s_running = false;
    s_vad_tracker = (companion_audio_vad_tracker_t){0};
    s_have_upload_token = false;
    s_last_upload_token = (companion_audio_token_t){0};
    s_was_vad_active = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    drain_playback_queue();
    if (NULL != s_output_lock &&
        pdTRUE == xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(20U))) {
        if (COMPANION_AUDIO_OUTPUT_SILENT == s_output_owner) {
            s_output_phase = COMPANION_AUDIO_OUTPUT_IDLE;
            s_output_token = (companion_audio_token_t){0};
        } else {
            s_output_phase = COMPANION_AUDIO_OUTPUT_STOPPING;
        }
        xSemaphoreGive(s_output_lock);
    }
    if (publish) {
        emit_event(COMPANION_AUDIO_EVENT_FATAL, 0U, 0U, error);
    }
}

static void feed_task(void *arg)
{
    (void)arg;
    const size_t tdm_frames = s_feed_chunk;
    int16_t *tdm_buffer = heap_caps_malloc(
        tdm_frames * COMPANION_AUDIO_TDM_CHANNELS * sizeof(int16_t),
        MALLOC_CAP_SPIRAM);
    int16_t *feed_buffer = heap_caps_malloc(
        tdm_frames * COMPANION_AUDIO_AFE_CHANNELS * sizeof(int16_t),
        MALLOC_CAP_SPIRAM);
    int16_t *snapshot_buffer = heap_caps_malloc(
        tdm_frames * 2U * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (NULL == tdm_buffer || NULL == feed_buffer ||
        NULL == snapshot_buffer) {
        ESP_LOGE(TAG, "feed allocation failed");
        stop_for_fatal_error(ESP_ERR_NO_MEM);
        heap_caps_free(tdm_buffer);
        heap_caps_free(feed_buffer);
        heap_caps_free(snapshot_buffer);
        mark_task_exit(AUDIO_FEED_EXIT_BIT, &s_feed_task);
        vTaskDelete(NULL);
        return;
    }

    (void)xEventGroupSetBits(s_task_events, AUDIO_FEED_READY_BIT);

    int64_t realtime_window_start_us = esp_timer_get_time();
    int64_t previous_feed_us = realtime_window_start_us;
    int64_t maximum_feed_gap_us = 0LL;
    uint32_t realtime_feed_blocks = 0U;
    int64_t total_read_us = 0LL;
    int64_t maximum_read_us = 0LL;
    uint32_t read_calls = 0U;
    int64_t total_processor_feed_us = 0LL;
    int64_t maximum_processor_feed_us = 0LL;
    uint32_t processor_feed_calls = 0U;
    audio_level_diag_t mic1_level = {0};
    audio_level_diag_t mic2_level = {0};
    audio_level_diag_t ref_raw_level = {0};
    audio_level_diag_t ref_x4_level = {0};
    audio_signal_diag_t signal_diag = {0};
    bool signal_diag_active = false;
    int64_t signal_diag_start_us = 0LL;
    const int64_t expected_block_us =
        (int64_t)(1000000ULL * (uint64_t)tdm_frames /
                  (uint64_t)OPUS_SAMPLE_RATE_HZ);
    while (s_running) {
        const int64_t read_start_us = esp_timer_get_time();
        esp_err_t result = board_laiwfs300_audio_read_tdm_4ch(tdm_buffer,
                                                               tdm_frames);
        const int64_t read_us = esp_timer_get_time() - read_start_us;
        total_read_us += read_us;
        read_calls++;
        if (read_us > maximum_read_us) {
            maximum_read_us = read_us;
        }
        if (ESP_OK != result) {
            s_stats.read_errors++;
            if (1U == s_stats.read_errors ||
                0U == (s_stats.read_errors % AUDIO_ERROR_LOG_INTERVAL)) {
                ESP_LOGW(TAG, "TDM read failed error=%s count=%lu",
                         esp_err_to_name(result),
                         (unsigned long)s_stats.read_errors);
            }
            vTaskDelay(1U);
            continue;
        }
        audio_output_diag_t block_output_diag = {0};
        (void)capture_output_diag(&block_output_diag);
        const bool next_signal_diag_active =
            block_output_diag.valid ? block_output_diag.aec_enabled :
                                       signal_diag_active;
        if (next_signal_diag_active != signal_diag_active) {
            audio_signal_diag_reset(&signal_diag, next_signal_diag_active);
            signal_diag_active = next_signal_diag_active;
            signal_diag_start_us = signal_diag_active ? read_start_us : 0LL;
        }
        for (size_t index = 0U; index < tdm_frames; ++index) {
            const size_t tdm_offset = index * COMPANION_AUDIO_TDM_CHANNELS;
            const int16_t mic1 = tdm_buffer[tdm_offset +
                                            COMPANION_AUDIO_MIC1_SLOT];
            const int16_t mic2 = tdm_buffer[tdm_offset +
                                            COMPANION_AUDIO_MIC2_SLOT];
            const int16_t ref_raw =
                tdm_buffer[tdm_offset + COMPANION_AUDIO_REF_SLOT];
            int32_t ref_scaled =
                (int32_t)ref_raw * COMPANION_AUDIO_REF_GAIN;
            const bool ref_scaled_clipped =
                ref_scaled > INT16_MAX || ref_scaled < INT16_MIN;
            audio_level_diag_add(&mic1_level, mic1,
                                 INT16_MAX == mic1 || INT16_MIN == mic1);
            audio_level_diag_add(&mic2_level, mic2,
                                 INT16_MAX == mic2 || INT16_MIN == mic2);
            audio_level_diag_add(&ref_raw_level, ref_raw,
                                 INT16_MAX == ref_raw || INT16_MIN == ref_raw);
            audio_level_diag_add(&ref_x4_level, ref_scaled,
                                 ref_scaled_clipped);
            audio_signal_diag_add(&signal_diag, mic1, mic2, ref_raw);
            if (ref_scaled > INT16_MAX) {
                ref_scaled = INT16_MAX;
            } else if (ref_scaled < INT16_MIN) {
                ref_scaled = INT16_MIN;
            }
            feed_buffer[index * COMPANION_AUDIO_AFE_CHANNELS] = mic1;
            feed_buffer[index * COMPANION_AUDIO_AFE_CHANNELS + 1U] = mic2;
            feed_buffer[index * COMPANION_AUDIO_AFE_CHANNELS + 2U] =
                (int16_t)ref_scaled;
            snapshot_buffer[index * 2U] = mic1;
            snapshot_buffer[index * 2U + 1U] = mic2;
        }
        update_ring(snapshot_buffer, tdm_frames);
        const int64_t processor_feed_start_us = esp_timer_get_time();
        result = audio_processor_feed(feed_buffer, tdm_frames);
        const int64_t processor_feed_us =
            esp_timer_get_time() - processor_feed_start_us;
        total_processor_feed_us += processor_feed_us;
        processor_feed_calls++;
        if (processor_feed_us > maximum_processor_feed_us) {
            maximum_processor_feed_us = processor_feed_us;
        }
        if (ESP_OK != result) {
            emit_event(COMPANION_AUDIO_EVENT_ERROR, 0U, 0U, result);
        }
        vTaskDelay(1U);
        s_stats.feed_blocks++;
        const int64_t feed_us = esp_timer_get_time();
        const int64_t feed_gap_us = feed_us - previous_feed_us;
        previous_feed_us = feed_us;
        if (feed_gap_us > maximum_feed_gap_us) {
            maximum_feed_gap_us = feed_gap_us;
        }
        if (signal_diag_active &&
            feed_gap_us > 2LL * expected_block_us) {
            signal_diag.late_blocks++;
        }
        realtime_feed_blocks++;
        const int64_t window_us = feed_us - realtime_window_start_us;
        if (AUDIO_TTS_REALTIME_LOG_INTERVAL_US <= window_us) {
            audio_output_diag_t output_diag = {0};
            (void)capture_output_diag(&output_diag);
            if (audio_diag_interval_us(&output_diag) > window_us) {
                continue;
            }
            const int64_t signal_window_us =
                signal_diag_active && 0LL < signal_diag_start_us ?
                feed_us - signal_diag_start_us : 0LL;
            log_audio_signal_diag(&signal_diag, signal_window_us, tdm_frames);
            ESP_LOGI(TAG,
                     "[DEBUG-AI-AUDIO] phase=feed rate=%.1f/s max_gap=%.1fms blocks=%lu read_avg=%.2fms read_max=%.2fms processor_avg=%.2fms processor_max=%.2fms mic1_rms=%.1f mic1_peak=%ld mic1_clip=%lu mic2_rms=%.1f mic2_peak=%ld mic2_clip=%lu ref_raw_rms=%.1f ref_raw_peak=%ld ref_raw_clip=%lu ref_x4_rms=%.1f ref_x4_peak=%ld ref_x4_clip=%lu state_valid=%u aec=%u owner=%d output_phase=%d generation=%lu wake_seq=%lu session=%lu request=%lu timestamp_ms=%llu",
                     (double)realtime_feed_blocks * 1000000.0 /
                         (double)window_us,
                     (double)maximum_feed_gap_us / 1000.0,
                     (unsigned long)realtime_feed_blocks,
                     0U < read_calls ?
                         (double)total_read_us / (double)read_calls / 1000.0 :
                         0.0,
                     (double)maximum_read_us / 1000.0,
                     0U < processor_feed_calls ?
                         (double)total_processor_feed_us /
                             (double)processor_feed_calls / 1000.0 :
                         0.0,
                     (double)maximum_processor_feed_us / 1000.0,
                     audio_level_diag_rms(&mic1_level),
                     (long)mic1_level.peak,
                     (unsigned long)mic1_level.clipped_samples,
                     audio_level_diag_rms(&mic2_level),
                     (long)mic2_level.peak,
                     (unsigned long)mic2_level.clipped_samples,
                     audio_level_diag_rms(&ref_raw_level),
                     (long)ref_raw_level.peak,
                     (unsigned long)ref_raw_level.clipped_samples,
                     audio_level_diag_rms(&ref_x4_level),
                     (long)ref_x4_level.peak,
                     (unsigned long)ref_x4_level.clipped_samples,
                     output_diag.valid ? 1U : 0U,
                     output_diag.aec_enabled ? 1U : 0U,
                     (int)output_diag.owner, (int)output_diag.phase,
                     (unsigned long)output_diag.token.generation,
                     (unsigned long)output_diag.token.wake_seq,
                     (unsigned long)output_diag.token.session_epoch,
                     (unsigned long)output_diag.token.request_id,
                     (unsigned long long)(feed_us / 1000LL));
            realtime_window_start_us = feed_us;
            maximum_feed_gap_us = 0LL;
            realtime_feed_blocks = 0U;
            total_read_us = 0LL;
            maximum_read_us = 0LL;
            read_calls = 0U;
            total_processor_feed_us = 0LL;
            maximum_processor_feed_us = 0LL;
            processor_feed_calls = 0U;
            mic1_level = (audio_level_diag_t){0};
            mic2_level = (audio_level_diag_t){0};
            ref_raw_level = (audio_level_diag_t){0};
            ref_x4_level = (audio_level_diag_t){0};
            audio_signal_diag_reset(&signal_diag, output_diag.valid &&
                                                     output_diag.aec_enabled);
            signal_diag_active = output_diag.valid && output_diag.aec_enabled;
            signal_diag_start_us = signal_diag_active ? feed_us : 0LL;
        }
    }

    heap_caps_free(tdm_buffer);
    heap_caps_free(feed_buffer);
    heap_caps_free(snapshot_buffer);
    mark_task_exit(AUDIO_FEED_EXIT_BIT, &s_feed_task);
    vTaskDelete(NULL);
}

static void handle_wake(const audio_level_diag_t *afe_block_level,
                        bool vad_active)
{
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (NULL != s_config.reserve_wake) {
        result = s_config.reserve_wake(&generation, &wake_seq,
                                       s_config.user_ctx);
    }
    if (ESP_OK != result) {
        ESP_LOGW(TAG, "wake reservation rejected result=%s",
                 esp_err_to_name(result));
        return;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    companion_audio_vad_tracker_arm_for_prompt(
        &s_vad_tracker, generation, wake_seq, vad_active);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    result = freeze_snapshot(wake_seq);
    audio_output_diag_t output_diag = {0};
    (void)capture_output_diag(&output_diag);
    ESP_LOGI(TAG,
             "[DEBUG-AI-AUDIO] event=wakenet generation=%lu wake_seq=%lu version=%lu result=%s afe_rms=%.1f afe_peak=%ld afe_clip=%lu state_valid=%u aec=%u owner=%d output_phase=%d output_generation=%lu output_wake_seq=%lu session=%lu request=%lu timestamp_ms=%llu",
             (unsigned long)generation, (unsigned long)wake_seq,
             (unsigned long)s_stats.snapshot_version, esp_err_to_name(result),
             audio_level_diag_rms(afe_block_level),
             NULL != afe_block_level ? (long)afe_block_level->peak : 0L,
             NULL != afe_block_level ?
                 (unsigned long)afe_block_level->clipped_samples : 0UL,
             output_diag.valid ? 1U : 0U,
             output_diag.aec_enabled ? 1U : 0U,
             (int)output_diag.owner, (int)output_diag.phase,
             (unsigned long)output_diag.token.generation,
             (unsigned long)output_diag.token.wake_seq,
             (unsigned long)output_diag.token.session_epoch,
             (unsigned long)output_diag.token.request_id,
             (unsigned long long)(esp_timer_get_time() / 1000LL));
    emit_event(COMPANION_AUDIO_EVENT_WAKE, generation, wake_seq, result);
}

static bool process_pending_prompt_reset(void)
{
    companion_audio_vad_prompt_token_t prompt_token = {0};
    portENTER_CRITICAL(&s_lifecycle_lock);
    const bool pending = companion_audio_vad_tracker_take_prompt_reset(
        &s_vad_tracker, &prompt_token);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (!pending) {
        return false;
    }

    const esp_err_t reset_result = audio_processor_reset_buffer();
    if (ESP_OK != reset_result) {
        ESP_LOGE(TAG, "Prompt AFE reset failed: %s",
                 esp_err_to_name(reset_result));
        stop_for_fatal_error(reset_result);
        return true;
    }

    bool completed = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    completed = companion_audio_vad_tracker_complete_prompt_reset(
        &s_vad_tracker, &prompt_token);
    if (completed) {
        s_was_vad_active = false;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (completed) {
        ESP_LOGI(TAG,
                 "[DEBUG-AI-AUDIO] event=prompt_vad_gate_open generation=%lu wake_seq=%lu session=%lu request=%lu timestamp_ms=%llu",
                 (unsigned long)prompt_token.generation,
                 (unsigned long)prompt_token.wake_seq,
                 (unsigned long)prompt_token.session_epoch,
                 (unsigned long)prompt_token.request_id,
                 (unsigned long long)(esp_timer_get_time() / 1000LL));
    }
    return true;
}

static void fetch_task(void *arg)
{
    (void)arg;
    int16_t *fetch_buffer = heap_caps_malloc(s_fetch_chunk * sizeof(int16_t),
                                             MALLOC_CAP_SPIRAM);
    int16_t *accumulator = heap_caps_malloc(OPUS_FRAME_SAMPLES * sizeof(int16_t),
                                            MALLOC_CAP_SPIRAM);
    if (NULL == fetch_buffer || NULL == accumulator) {
        ESP_LOGE(TAG, "fetch allocation failed");
        stop_for_fatal_error(ESP_ERR_NO_MEM);
        heap_caps_free(fetch_buffer);
        heap_caps_free(accumulator);
        mark_task_exit(AUDIO_FETCH_EXIT_BIT, &s_fetch_task);
        vTaskDelete(NULL);
        return;
    }

    (void)xEventGroupSetBits(s_task_events, AUDIO_FETCH_READY_BIT);

    size_t accumulator_offset = 0U;
    companion_audio_token_t accumulator_token = {0};
    uint32_t vad_active_fetches = 0U;
    int64_t vad_start_us = 0LL;
    uint32_t vad_start_feed_blocks = 0U;
    uint32_t vad_start_fetch_blocks = 0U;
    uint32_t voice_evidence_fetches = 0U;
    uint32_t voice_rejected_fetches = 0U;
    companion_audio_voice_gate_config_t voice_gate_config = {0};
    companion_audio_voice_gate_config_default(&voice_gate_config);
    companion_audio_voice_gate_t voice_gate = {0};
    companion_audio_voice_gate_init(&voice_gate, &voice_gate_config);
    int64_t realtime_window_start_us = esp_timer_get_time();
    int64_t previous_fetch_us = realtime_window_start_us;
    int64_t maximum_fetch_gap_us = 0LL;
    uint32_t realtime_fetch_blocks = 0U;
    int64_t total_processor_fetch_us = 0LL;
    int64_t maximum_processor_fetch_us = 0LL;
    uint32_t processor_fetch_calls = 0U;
    audio_level_diag_t afe_window_level = {0};
    audio_level_diag_t vad_level = {0};
    while (s_running) {
        if (process_pending_prompt_reset()) {
            continue;
        }
        size_t fetched = 0U;
        bool vad_active = false;
        bool wake = false;
        const int64_t processor_fetch_start_us = esp_timer_get_time();
        esp_err_t result = audio_processor_fetch(fetch_buffer, &fetched,
                                                  &vad_active, &wake);
        const int64_t processor_fetch_us =
            esp_timer_get_time() - processor_fetch_start_us;
        total_processor_fetch_us += processor_fetch_us;
        processor_fetch_calls++;
        if (processor_fetch_us > maximum_processor_fetch_us) {
            maximum_processor_fetch_us = processor_fetch_us;
        }
        if (ESP_OK != result || 0U == fetched) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        companion_audio_token_t upload_token = {0};
        bool upload_token_valid = get_upload_token(&upload_token);
        (void)retire_stale_upload_capture(
            &upload_token, upload_token_valid);
        if (vad_active && !s_was_vad_active) {
            vad_level = (audio_level_diag_t){0};
            voice_evidence_fetches = 0U;
            voice_rejected_fetches = 0U;
        }
        audio_level_diag_t afe_block_level = {0};
        for (size_t index = 0U; index < fetched; ++index) {
            const int16_t sample = fetch_buffer[index];
            const bool clipped = INT16_MAX == sample || INT16_MIN == sample;
            audio_level_diag_add(&afe_block_level, sample, clipped);
            audio_level_diag_add(&afe_window_level, sample, clipped);
            if (vad_active) {
                audio_level_diag_add(&vad_level, sample, clipped);
            }
        }
        s_stats.fetch_blocks++;
        const int64_t fetch_us = esp_timer_get_time();
        const int64_t fetch_gap_us = fetch_us - previous_fetch_us;
        previous_fetch_us = fetch_us;
        if (fetch_gap_us > maximum_fetch_gap_us) {
            maximum_fetch_gap_us = fetch_gap_us;
        }
        realtime_fetch_blocks++;
        const int64_t window_us = fetch_us - realtime_window_start_us;
        if (AUDIO_TTS_REALTIME_LOG_INTERVAL_US <= window_us) {
            audio_output_diag_t output_diag = {0};
            (void)capture_output_diag(&output_diag);
            if (audio_diag_interval_us(&output_diag) <= window_us) {
                ESP_LOGI(TAG,
                         "[DEBUG-AI-AUDIO] phase=fetch rate=%.1f/s max_gap=%.1fms blocks=%lu processor_avg=%.2fms processor_max=%.2fms afe_rms=%.1f afe_peak=%ld afe_clip=%lu vad=%u state_valid=%u aec=%u owner=%d output_phase=%d generation=%lu wake_seq=%lu session=%lu request=%lu timestamp_ms=%llu",
                         (double)realtime_fetch_blocks * 1000000.0 /
                             (double)window_us,
                         (double)maximum_fetch_gap_us / 1000.0,
                         (unsigned long)realtime_fetch_blocks,
                         0U < processor_fetch_calls ?
                             (double)total_processor_fetch_us /
                                 (double)processor_fetch_calls / 1000.0 :
                             0.0,
                         (double)maximum_processor_fetch_us / 1000.0,
                         audio_level_diag_rms(&afe_window_level),
                         (long)afe_window_level.peak,
                         (unsigned long)afe_window_level.clipped_samples,
                         vad_active ? 1U : 0U,
                         output_diag.valid ? 1U : 0U,
                         output_diag.aec_enabled ? 1U : 0U,
                         (int)output_diag.owner, (int)output_diag.phase,
                         (unsigned long)output_diag.token.generation,
                         (unsigned long)output_diag.token.wake_seq,
                         (unsigned long)output_diag.token.session_epoch,
                         (unsigned long)output_diag.token.request_id,
                         (unsigned long long)(fetch_us / 1000LL));
                realtime_window_start_us = fetch_us;
                maximum_fetch_gap_us = 0LL;
                realtime_fetch_blocks = 0U;
                total_processor_fetch_us = 0LL;
                maximum_processor_fetch_us = 0LL;
                processor_fetch_calls = 0U;
                afe_window_level = (audio_level_diag_t){0};
            }
        }
        if (wake) {
            handle_wake(&afe_block_level, vad_active);
        }
        companion_audio_voice_features_t voice_features = {0};
        const bool voice_features_valid =
            companion_audio_voice_features_from_pcm(
                fetch_buffer, fetched, &voice_features);
        const companion_audio_voice_gate_result_t voice_gate_result =
            companion_audio_voice_gate_step(
                &voice_gate, vad_active, &voice_features,
                (uint64_t)(fetch_us / 1000LL));
        if (vad_active) {
            if (voice_features_valid && voice_gate_result.evidence_active) {
                voice_evidence_fetches++;
            } else {
                voice_rejected_fetches++;
            }
        }
        upload_token = (companion_audio_token_t){0};
        upload_token_valid = get_upload_token(&upload_token);
        bool upload_capture_closed = retire_stale_upload_capture(
            &upload_token, upload_token_valid);
        if (upload_token_valid) {
            remember_upload_token(&upload_token);
            if (!bind_vad_upload_token(&upload_token)) {
                upload_capture_closed = true;
            }
        }
        if (vad_active && !s_was_vad_active) {
            vad_active_fetches = 0U;
            vad_start_us = fetch_us;
            vad_start_feed_blocks = s_stats.feed_blocks;
            vad_start_fetch_blocks = s_stats.fetch_blocks;
        }
        if (vad_active) {
            vad_active_fetches++;
        }
        companion_audio_vad_result_t vad_result = {0};
        portENTER_CRITICAL(&s_lifecycle_lock);
        vad_result = companion_audio_vad_tracker_step_with_voice(
            &s_vad_tracker, vad_active,
            voice_features_valid && voice_gate_result.evidence_active,
            (uint64_t)(fetch_us / 1000LL));
        portEXIT_CRITICAL(&s_lifecycle_lock);
        upload_token = (companion_audio_token_t){0};
        upload_token_valid = get_upload_token(&upload_token);
        if (retire_stale_upload_capture(&upload_token, upload_token_valid)) {
            upload_capture_closed = true;
        }
        if (upload_token_valid) {
            remember_upload_token(&upload_token);
            if (!bind_vad_upload_token(&upload_token)) {
                upload_capture_closed = true;
            }
        }
        if (upload_capture_closed) {
            vad_result = (companion_audio_vad_result_t){0};
        }
        if (0U != (vad_result.actions &
                   COMPANION_AUDIO_VAD_ACTION_START)) {
            ESP_LOGI(TAG,
                     "[DEBUG-AI-AUDIO] event=vad_start generation=%lu wake_seq=%lu feed=%lu fetch=%lu accumulated_ms=%lu timestamp_ms=%llu",
                     (unsigned long)vad_result.generation,
                     (unsigned long)vad_result.wake_seq,
                     (unsigned long)vad_start_feed_blocks,
                     (unsigned long)vad_start_fetch_blocks,
                     (unsigned long)vad_result.accumulated_active_ms,
                     (unsigned long long)(fetch_us / 1000LL));
            emit_event(COMPANION_AUDIO_EVENT_VAD_START,
                       vad_result.generation, vad_result.wake_seq, ESP_OK);
        }
        if (0U != (vad_result.actions &
                   COMPANION_AUDIO_VAD_ACTION_SPEECH_CONFIRMED)) {
            ESP_LOGI(TAG,
                     "[DEBUG-AI-AUDIO] event=speech_confirmed generation=%lu wake_seq=%lu window_ms=%lu accumulated_ms=%lu noise_rms=%lu snr_milli=%lu crest_milli=%lu zcr_permille=%lu accepted_fetches=%lu rejected_fetches=%lu timestamp_ms=%llu",
                     (unsigned long)vad_result.generation,
                     (unsigned long)vad_result.wake_seq,
                     (unsigned long)vad_result.window_duration_ms,
                     (unsigned long)vad_result.accumulated_active_ms,
                     (unsigned long)voice_gate_result.noise_rms,
                     (unsigned long)voice_gate_result.snr_milli,
                     (unsigned long)voice_gate_result.crest_milli,
                     (unsigned long)voice_gate_result.zero_crossing_permille,
                     (unsigned long)voice_evidence_fetches,
                     (unsigned long)voice_rejected_fetches,
                     (unsigned long long)(fetch_us / 1000LL));
            emit_event(COMPANION_AUDIO_EVENT_SPEECH_CONFIRMED,
                       vad_result.generation, vad_result.wake_seq, ESP_OK);
        }
        if (0U != (vad_result.actions &
                   COMPANION_AUDIO_VAD_ACTION_END)) {
            ESP_LOGI(TAG,
                     "[DEBUG-AI-AUDIO] event=vad_end generation=%lu wake_seq=%lu accumulated_ms=%lu tail_ms=%lu noise_rms=%lu snr_milli=%lu crest_milli=%lu zcr_permille=%lu accepted_fetches=%lu rejected_fetches=%lu timestamp_ms=%llu",
                     (unsigned long)vad_result.generation,
                     (unsigned long)vad_result.wake_seq,
                     (unsigned long)vad_result.accumulated_active_ms,
                     (unsigned long)vad_result.tail_silence_ms,
                     (unsigned long)voice_gate_result.noise_rms,
                     (unsigned long)voice_gate_result.snr_milli,
                     (unsigned long)voice_gate_result.crest_milli,
                     (unsigned long)voice_gate_result.zero_crossing_permille,
                     (unsigned long)voice_evidence_fetches,
                     (unsigned long)voice_rejected_fetches,
                     (unsigned long long)(fetch_us / 1000LL));
            emit_event(COMPANION_AUDIO_EVENT_VAD_END,
                       vad_result.generation, vad_result.wake_seq, ESP_OK);
        }
        if (!vad_active && s_was_vad_active) {
            const int64_t vad_duration_us =
                (0LL < vad_start_us) ? fetch_us - vad_start_us : 0LL;
            ESP_LOGI(TAG,
                     "[DEBUG-AI-AUDIO] event=vad_window_end generation=%lu wake_seq=%lu duration=%.1fms active_fetches=%lu accepted_fetches=%lu rejected_fetches=%lu feed_delta=%lu fetch_delta=%lu afe_rms=%.1f afe_peak=%ld afe_clip=%lu noise_rms=%lu timestamp_ms=%llu",
                     (unsigned long)s_vad_tracker.capture_generation,
                     (unsigned long)s_vad_tracker.capture_wake_seq,
                     (double)vad_duration_us / 1000.0,
                     (unsigned long)vad_active_fetches,
                     (unsigned long)voice_evidence_fetches,
                     (unsigned long)voice_rejected_fetches,
                     (unsigned long)(s_stats.feed_blocks -
                                     vad_start_feed_blocks),
                     (unsigned long)(s_stats.fetch_blocks -
                                     vad_start_fetch_blocks),
                     audio_level_diag_rms(&vad_level),
                     (long)vad_level.peak,
                     (unsigned long)vad_level.clipped_samples,
                     (unsigned long)voice_gate_result.noise_rms,
                     (unsigned long long)(fetch_us / 1000LL));
            vad_start_us = 0LL;
            vad_level = (audio_level_diag_t){0};
        }
        s_was_vad_active = upload_capture_closed ? false : vad_active;

        if (!upload_token_valid) {
            accumulator_offset = 0U;
            accumulator_token = (companion_audio_token_t){0};
            clear_pcm_queue();
            vTaskDelay(1U);
            continue;
        }
        if (0U != accumulator_offset &&
            !token_matches(&accumulator_token, &upload_token)) {
            accumulator_offset = 0U;
        }
        accumulator_token = upload_token;
        size_t remaining = fetched;
        size_t source_offset = 0U;
        while (0U < remaining) {
            size_t to_copy = OPUS_FRAME_SAMPLES - accumulator_offset;
            if (to_copy > remaining) {
                to_copy = remaining;
            }
            memcpy(&accumulator[accumulator_offset],
                   &fetch_buffer[source_offset], to_copy * sizeof(int16_t));
            accumulator_offset += to_copy;
            source_offset += to_copy;
            remaining -= to_copy;
            if (accumulator_offset >= OPUS_FRAME_SAMPLES) {
                (void)enqueue_pcm_frame(accumulator,
                                        &accumulator_token);
                accumulator_offset = 0U;
            }
        }
        vTaskDelay(1U);
    }
    heap_caps_free(fetch_buffer);
    heap_caps_free(accumulator);
    mark_task_exit(AUDIO_FETCH_EXIT_BIT, &s_fetch_task);
    vTaskDelete(NULL);
}

static void encode_task(void *arg)
{
    (void)arg;
    companion_audio_pcm_frame_t *frame = heap_caps_malloc(
        sizeof(*frame), MALLOC_CAP_SPIRAM);
    uint8_t *opus_buffer = heap_caps_malloc(OPUS_BUFFER_BYTES,
                                            MALLOC_CAP_SPIRAM);
    if (NULL == frame || NULL == opus_buffer) {
        ESP_LOGE(TAG, "encode allocation failed");
        stop_for_fatal_error(ESP_ERR_NO_MEM);
        heap_caps_free(frame);
        heap_caps_free(opus_buffer);
        mark_task_exit(AUDIO_ENCODE_EXIT_BIT, &s_encode_task);
        vTaskDelete(NULL);
        return;
    }

    (void)xEventGroupSetBits(s_task_events, AUDIO_ENCODE_READY_BIT);
    int64_t log_window_start_us = esp_timer_get_time();
    uint32_t window_encoded = 0U;
    while (s_running) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));
        while (s_running && ESP_OK == dequeue_pcm_frame(frame)) {
            companion_audio_token_t current_token = {0};
            if (!get_upload_token(&current_token) ||
                !token_matches(&frame->token, &current_token)) {
                s_stats.encode_gate_drops++;
                continue;
            }
            size_t opus_length = OPUS_BUFFER_BYTES;
            const int64_t encode_start_us = esp_timer_get_time();
            const esp_err_t result = opus_codec_encode(
                frame->samples, OPUS_FRAME_SAMPLES,
                opus_buffer, &opus_length);
            const uint32_t encode_us = (uint32_t)(
                esp_timer_get_time() - encode_start_us);
            if (encode_us > s_stats.encode_max_us) {
                s_stats.encode_max_us = encode_us;
            }
            if (ESP_OK != result) {
                s_stats.encode_errors++;
                continue;
            }
            current_token = (companion_audio_token_t){0};
            if (!get_upload_token(&current_token) ||
                !token_matches(&frame->token, &current_token)) {
                s_stats.encode_gate_drops++;
                continue;
            }
            if (0U < opus_length && NULL != s_config.on_opus) {
                s_config.on_opus(opus_buffer, (int)opus_length,
                                 &frame->token,
                                 s_config.user_ctx);
                s_stats.encoded_frames++;
                window_encoded++;
            }
        }
        const int64_t current_us = esp_timer_get_time();
        if (AUDIO_REALTIME_LOG_INTERVAL_US <=
            current_us - log_window_start_us) {
            ESP_LOGI(TAG,
                     "[DEBUG-AI-P0] phase=audio_encode encoded=%lu queue_depth=%lu queue_high=%lu queue_drop=%lu gate_drop=%lu errors=%lu max_ms=%.2f timestamp_ms=%llu",
                     (unsigned long)window_encoded,
                     (unsigned long)s_stats.pcm_queue_depth,
                     (unsigned long)s_stats.pcm_queue_high_water,
                     (unsigned long)s_stats.pcm_queue_drops,
                     (unsigned long)s_stats.encode_gate_drops,
                     (unsigned long)s_stats.encode_errors,
                     (double)s_stats.encode_max_us / 1000.0,
                     (unsigned long long)(current_us / 1000LL));
            window_encoded = 0U;
            log_window_start_us = current_us;
        }
    }
    clear_pcm_queue();
    heap_caps_free(frame);
    heap_caps_free(opus_buffer);
    mark_task_exit(AUDIO_ENCODE_EXIT_BIT, &s_encode_task);
    vTaskDelete(NULL);
}

static void playback_task_fn(void *arg)
{
    (void)arg;
    int16_t *decode_buffer = heap_caps_malloc(
        OPUS_DECODE_FRAME_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *play_buffer = heap_caps_malloc(
        PLAYBACK_FRAME_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (NULL == decode_buffer || NULL == play_buffer) {
        ESP_LOGE(TAG, "playback allocation failed");
        stop_for_fatal_error(ESP_ERR_NO_MEM);
        heap_caps_free(decode_buffer);
        heap_caps_free(play_buffer);
        mark_task_exit(AUDIO_PLAY_EXIT_BIT, &s_playback_task);
        vTaskDelete(NULL);
        return;
    }

    (void)xEventGroupSetBits(s_task_events, AUDIO_PLAY_READY_BIT);

    int64_t playback_window_start_us = 0LL;
    uint32_t realtime_playback_frames = 0U;
    uint32_t maximum_queue_depth = 0U;
    int64_t total_write_us = 0LL;
    int64_t maximum_write_us = 0LL;
    uint32_t write_calls = 0U;

    while (s_running) {
        playback_item_t item = {0};
        if (pdTRUE != xQueueReceive(s_playback_queue, &item,
                                    pdMS_TO_TICKS(100))) {
            playback_window_start_us = 0LL;
            realtime_playback_frames = 0U;
            maximum_queue_depth = 0U;
            total_write_us = 0LL;
            maximum_write_us = 0LL;
            write_calls = 0U;
            continue;
        }
        if (NULL == item.data) {
            continue;
        }
        if (NULL == s_output_lock ||
            pdTRUE != xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(100))) {
            heap_caps_free(item.data);
            continue;
        }
        if (COMPANION_AUDIO_OUTPUT_TTS != s_output_owner ||
            COMPANION_AUDIO_OUTPUT_ACTIVE != s_output_phase ||
            !token_matches(&item.token, &s_output_token)) {
            xSemaphoreGive(s_output_lock);
            heap_caps_free(item.data);
            continue;
        }
        xSemaphoreGive(s_output_lock);
        const int64_t frame_start_us = esp_timer_get_time();
        if (0LL == playback_window_start_us) {
            playback_window_start_us = frame_start_us;
        }
        const UBaseType_t queue_depth = uxQueueMessagesWaiting(
            s_playback_queue);
        if (queue_depth > maximum_queue_depth) {
            maximum_queue_depth = (uint32_t)queue_depth;
        }
        size_t decoded_samples = OPUS_DECODE_FRAME_SAMPLES;
        esp_err_t result = opus_codec_decode(item.data, (size_t)item.length,
                                             decode_buffer, &decoded_samples);
        size_t play_samples = 0U;
        if (ESP_OK == result && 0U < decoded_samples) {
            play_samples = resample_to_playback(
                decode_buffer, decoded_samples, play_buffer,
                PLAYBACK_FRAME_SAMPLES);
        }
        if (ESP_OK == result && 0U < play_samples) {
            if (NULL == s_output_lock ||
                pdTRUE != xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(100))) {
                result = ESP_ERR_TIMEOUT;
            } else if (COMPANION_AUDIO_OUTPUT_TTS != s_output_owner ||
                       COMPANION_AUDIO_OUTPUT_ACTIVE != s_output_phase ||
                       !token_matches(&item.token, &s_output_token)) {
                ESP_LOGI(TAG,
                         "[DEBUG-AI-P0] phase=playback_stale_drop generation=%lu wake_seq=%lu request=%lu timestamp_ms=%llu",
                         (unsigned long)item.token.generation,
                         (unsigned long)item.token.wake_seq,
                         (unsigned long)item.token.request_id,
                         (unsigned long long)(esp_timer_get_time() / 1000LL));
                xSemaphoreGive(s_output_lock);
                heap_caps_free(item.data);
                continue;
            } else {
                const int64_t write_start_us = esp_timer_get_time();
                result = esp_codec_dev_write(s_output_device, play_buffer,
                                             play_samples * sizeof(int16_t));
                const int64_t write_us = esp_timer_get_time() - write_start_us;
                total_write_us += write_us;
                write_calls++;
                if (write_us > maximum_write_us) {
                    maximum_write_us = write_us;
                }
                xSemaphoreGive(s_output_lock);
            }
        }
        if (ESP_OK != result) {
            ESP_LOGW(TAG, "playback frame failed: %s", esp_err_to_name(result));
        }
        heap_caps_free(item.data);
        realtime_playback_frames++;
        const int64_t playback_us = esp_timer_get_time();
        const int64_t window_us = playback_us - playback_window_start_us;
        if (AUDIO_PLAYBACK_REALTIME_LOG_INTERVAL_US <= window_us) {
            audio_output_diag_t output_diag = {0};
            (void)capture_output_diag(&output_diag);
            ESP_LOGI(TAG,
                     "[DEBUG-AI-AUDIO] phase=playback rate=%.1f/s write_avg=%.2fms write_max=%.2fms queue_peak=%lu frames=%lu state_valid=%u aec=%u owner=%d output_phase=%d generation=%lu wake_seq=%lu session=%lu request=%lu timestamp_ms=%llu",
                     (double)realtime_playback_frames * 1000000.0 /
                         (double)window_us,
                     0U < write_calls ?
                         (double)total_write_us / (double)write_calls / 1000.0 :
                         0.0,
                     (double)maximum_write_us / 1000.0,
                     (unsigned long)maximum_queue_depth,
                     (unsigned long)realtime_playback_frames,
                     output_diag.valid ? 1U : 0U,
                     output_diag.aec_enabled ? 1U : 0U,
                     (int)output_diag.owner, (int)output_diag.phase,
                     (unsigned long)output_diag.token.generation,
                     (unsigned long)output_diag.token.wake_seq,
                     (unsigned long)output_diag.token.session_epoch,
                     (unsigned long)output_diag.token.request_id,
                     (unsigned long long)(playback_us / 1000LL));
            playback_window_start_us = playback_us;
            realtime_playback_frames = 0U;
            maximum_queue_depth = 0U;
            total_write_us = 0LL;
            maximum_write_us = 0LL;
            write_calls = 0U;
        }
    }
    heap_caps_free(decode_buffer);
    heap_caps_free(play_buffer);
    mark_task_exit(AUDIO_PLAY_EXIT_BIT, &s_playback_task);
    vTaskDelete(NULL);
}

static void prompt_apply_fade(int16_t *pcm, int samples)
{
    if (NULL == pcm || 0 >= samples || 0 >= s_prompt_fade_remaining) {
        return;
    }
    const int fade_samples = (samples < s_prompt_fade_remaining) ?
                             samples : s_prompt_fade_remaining;
    const int processed = PROMPT_FADE_IN_SAMPLES - s_prompt_fade_remaining;
    for (int index = 0; index < fade_samples; ++index) {
        pcm[index] = (int16_t)(((int32_t)pcm[index] *
                                (processed + index + 1)) /
                               PROMPT_FADE_IN_SAMPLES);
    }
    s_prompt_fade_remaining -= fade_samples;
}

static void prompt_write_silence(void)
{
    int16_t silence[PROMPT_PREROLL_SAMPLES] = {0};
    if (NULL != s_output_device) {
        (void)esp_codec_dev_write(s_output_device, silence, sizeof(silence));
    }
}

static int prompt_output_cb(uint8_t *data, int data_size, void *ctx)
{
    (void)ctx;
    if (NULL == data || 0 >= data_size || NULL == s_output_device) {
        return 0;
    }
    if (!audio_runtime_accepts_output()) {
        return 0;
    }
    if (NULL == s_output_lock ||
        pdTRUE != xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(20))) {
        return 0;
    }
    if (COMPANION_AUDIO_OUTPUT_PROMPT != s_output_owner ||
        COMPANION_AUDIO_OUTPUT_ACTIVE != s_output_phase) {
        xSemaphoreGive(s_output_lock);
        return 0;
    }
    prompt_apply_fade((int16_t *)data, data_size / (int)sizeof(int16_t));
    (void)esp_codec_dev_write(s_output_device, data, data_size);
    xSemaphoreGive(s_output_lock);
    return 0;
}

static int prompt_event_cb(esp_asp_event_pkt_t *event, void *ctx)
{
    (void)ctx;
    esp_asp_state_t state = 0;
    if (NULL == event || ESP_ASP_EVENT_TYPE_STATE != event->type ||
        NULL == event->payload || sizeof(state) != event->payload_size) {
        return 0;
    }
    memcpy(&state, event->payload, sizeof(state));
    if (ESP_ASP_STATE_STOPPED == state || ESP_ASP_STATE_FINISHED == state ||
        ESP_ASP_STATE_ERROR == state) {
        companion_audio_token_t terminal_token = {0};
        s_prompt_state = PROMPT_STATE_IDLE;
        s_prompt_fade_remaining = 0;
        bool owner_closed = false;
        if (NULL != s_output_lock &&
            pdTRUE == xSemaphoreTake(
                s_output_lock,
                pdMS_TO_TICKS(AUDIO_OUTPUT_STOP_TIMEOUT_MS))) {
            terminal_token = s_prompt_callback_token;
            if (output_identity_matches(COMPANION_AUDIO_OUTPUT_PROMPT,
                                        &terminal_token)) {
                s_output_owner = COMPANION_AUDIO_OUTPUT_SILENT;
                s_output_phase = COMPANION_AUDIO_OUTPUT_IDLE;
                s_output_token = (companion_audio_token_t){0};
                s_prompt_callback_token = (companion_audio_token_t){0};
                owner_closed = true;
            } else if (COMPANION_AUDIO_OUTPUT_PROMPT != s_output_owner) {
                owner_closed = true;
            }
            xSemaphoreGive(s_output_lock);
        }
        if (!owner_closed) {
            stop_for_fatal_error(ESP_ERR_TIMEOUT);
        }
        if (NULL != s_prompt_terminal) {
            (void)xSemaphoreGive(s_prompt_terminal);
        }
        ESP_LOGI(TAG,
                 "[DEBUG-WSRAM] stage=prompt_terminal prompt_state=%d internal_free=%lu internal_largest=%lu internal_min=%lu psram_free=%lu",
                 (int)state,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_minimum_free_size(
                     MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        if (!request_vad_prompt_reset(&terminal_token)) {
            ESP_LOGW(TAG,
                     "stale Prompt terminal ignored generation=%lu wake_seq=%lu session=%lu request=%lu",
                     (unsigned long)terminal_token.generation,
                     (unsigned long)terminal_token.wake_seq,
                     (unsigned long)terminal_token.session_epoch,
                     (unsigned long)terminal_token.request_id);
        }
    }
    return 0;
}

static esp_err_t prompt_init(void)
{
    s_prompt_ready = false;
    s_prompt_lock = xSemaphoreCreateMutex();
    if (NULL == s_prompt_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_prompt_terminal = xSemaphoreCreateBinary();
    if (NULL == s_prompt_terminal) {
        vSemaphoreDelete(s_prompt_lock);
        s_prompt_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    const esp_asp_cfg_t config = {
        .in = {.cb = NULL, .user_ctx = NULL},
        .out = {.cb = prompt_output_cb, .user_ctx = NULL},
        .task_prio = 5,
    };
    esp_err_t result = esp_audio_simple_player_new(&config, &s_prompt_player);
    if (ESP_OK != result || NULL == s_prompt_player) {
        if (NULL != s_prompt_player) {
            esp_audio_simple_player_destroy(s_prompt_player);
            s_prompt_player = NULL;
        }
        vSemaphoreDelete(s_prompt_terminal);
        vSemaphoreDelete(s_prompt_lock);
        s_prompt_terminal = NULL;
        s_prompt_lock = NULL;
        return ESP_FAIL;
    }
    result = esp_audio_simple_player_set_event(s_prompt_player,
                                               prompt_event_cb, NULL);
    if (ESP_OK != result) {
        esp_audio_simple_player_destroy(s_prompt_player);
        s_prompt_player = NULL;
        vSemaphoreDelete(s_prompt_terminal);
        vSemaphoreDelete(s_prompt_lock);
        s_prompt_terminal = NULL;
        s_prompt_lock = NULL;
    }
    s_prompt_ready = ESP_OK == result;
    return result;
}

static void release_init_resources(void)
{
    if (NULL != s_prompt_player) {
        esp_audio_simple_player_destroy(s_prompt_player);
        s_prompt_player = NULL;
    }
    if (NULL != s_prompt_terminal) {
        vSemaphoreDelete(s_prompt_terminal);
        s_prompt_terminal = NULL;
    }
    if (NULL != s_prompt_lock) {
        vSemaphoreDelete(s_prompt_lock);
        s_prompt_lock = NULL;
    }
    if (NULL != s_task_events) {
        vEventGroupDelete(s_task_events);
        s_task_events = NULL;
    }
    if (NULL != s_output_lock) {
        vSemaphoreDelete(s_output_lock);
        s_output_lock = NULL;
    }
    if (NULL != s_snapshot_lock) {
        vSemaphoreDelete(s_snapshot_lock);
        s_snapshot_lock = NULL;
    }
    if (NULL != s_pcm_queue_lock) {
        vSemaphoreDelete(s_pcm_queue_lock);
        s_pcm_queue_lock = NULL;
    }
    heap_caps_free(s_ring_mic1);
    heap_caps_free(s_ring_mic2);
    heap_caps_free(s_frozen_mic1);
    heap_caps_free(s_frozen_mic2);
    s_ring_mic1 = NULL;
    s_ring_mic2 = NULL;
    s_frozen_mic1 = NULL;
    s_frozen_mic2 = NULL;
    heap_caps_free(s_pcm_storage);
    s_pcm_storage = NULL;
    s_pcm_queue = (companion_audio_pcm_queue_t){0};
    s_feed_chunk = 0U;
    s_fetch_chunk = 0U;
    s_output_device = NULL;
    s_prompt_ready = false;
    s_tts_aec_enabled = false;
    opus_codec_deinit();
    audio_processor_deinit();
}

esp_err_t companion_audio_init(const companion_audio_config_t *config)
{
    if (NULL == config || NULL == config->reserve_wake ||
        NULL == config->on_event || NULL == config->on_opus ||
        NULL == config->get_upload_token) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized) {
        return ESP_OK;
    }
    s_config = *config;
    memset(&s_stats, 0, sizeof(s_stats));

    esp_err_t result = board_laiwfs300_audio_init();
    if (ESP_OK == result) {
        result = board_laiwfs300_audio_open_input_all_channels();
    }
    if (ESP_OK != result) {
        return result;
    }
    s_output_device = board_laiwfs300_audio_get_output_dev();
    if (NULL == s_output_device) {
        return ESP_ERR_INVALID_STATE;
    }
    const audio_processor_config_t processor_config =
        companion_audio_make_processor_config();
    result = audio_processor_init(&processor_config);
    if (ESP_OK != result) {
        release_init_resources();
        return result;
    }
    result = audio_processor_disable_aec();
    if (ESP_OK != result) {
        release_init_resources();
        return result;
    }
    s_tts_aec_enabled = false;
    s_feed_chunk = audio_processor_get_feed_chunksize();
    s_fetch_chunk = audio_processor_get_fetch_chunksize();
    if (0U == s_feed_chunk || 0U == s_fetch_chunk) {
        release_init_resources();
        return ESP_ERR_INVALID_STATE;
    }

    const opus_encoder_config_t encoder_config = {
        .sample_rate = OPUS_SAMPLE_RATE_HZ,
        .channels = 1,
        .frame_duration_ms = COMPANION_AUDIO_OPUS_FRAME_MS,
        .bitrate = COMPANION_AUDIO_OPUS_BITRATE,
        .complexity = 0,
        .enable_vbr = true,
        .enable_dtx = true,
    };
    result = opus_codec_encoder_init(&encoder_config);
    if (ESP_OK != result) {
        release_init_resources();
        return result;
    }
    const opus_decoder_config_t decoder_config = {
        .sample_rate = OPUS_DECODE_SAMPLE_RATE_HZ,
        .channels = 1,
        .frame_duration_ms = COMPANION_AUDIO_OPUS_FRAME_MS,
    };
    result = opus_codec_decoder_init(&decoder_config);
    if (ESP_OK != result) {
        release_init_resources();
        return result;
    }

    s_snapshot_lock = xSemaphoreCreateMutex();
    s_output_lock = xSemaphoreCreateMutex();
    s_pcm_queue_lock = xSemaphoreCreateMutex();
    s_task_events = xEventGroupCreate();
    const size_t snapshot_bytes = COMPANION_AUDIO_SNAPSHOT_FRAMES *
                                  sizeof(int16_t);
    s_ring_mic1 = heap_caps_malloc(snapshot_bytes, MALLOC_CAP_SPIRAM);
    s_ring_mic2 = heap_caps_malloc(snapshot_bytes, MALLOC_CAP_SPIRAM);
    s_frozen_mic1 = heap_caps_malloc(snapshot_bytes, MALLOC_CAP_SPIRAM);
    s_frozen_mic2 = heap_caps_malloc(snapshot_bytes, MALLOC_CAP_SPIRAM);
    s_pcm_storage = heap_caps_calloc(COMPANION_AUDIO_PCM_QUEUE_DEPTH,
                                     sizeof(*s_pcm_storage),
                                     MALLOC_CAP_SPIRAM);
    if (NULL == s_snapshot_lock || NULL == s_output_lock ||
        NULL == s_pcm_queue_lock || NULL == s_task_events ||
        NULL == s_ring_mic1 || NULL == s_ring_mic2 ||
        NULL == s_frozen_mic1 || NULL == s_frozen_mic2 ||
        NULL == s_pcm_storage) {
        release_init_resources();
        return ESP_ERR_NO_MEM;
    }
    result = companion_audio_pcm_queue_init(
        &s_pcm_queue, s_pcm_storage, COMPANION_AUDIO_PCM_QUEUE_DEPTH);
    if (ESP_OK != result) {
        release_init_resources();
        return result;
    }

    enable_amplifier();
    (void)esp_codec_dev_set_out_vol(s_output_device,
                                    COMPANION_AUDIO_OUTPUT_VOLUME);
    result = prompt_init();
    if (ESP_OK != result) {
        ESP_LOGW(TAG, "prompt unavailable: %s", esp_err_to_name(result));
    }
    s_output_owner = COMPANION_AUDIO_OUTPUT_SILENT;
    s_output_phase = COMPANION_AUDIO_OUTPUT_IDLE;
    s_output_token = (companion_audio_token_t){0};
    s_prompt_callback_token = (companion_audio_token_t){0};
    s_fatal_reported = false;
    s_fatal_error = ESP_OK;
    s_starting = false;
    s_stop_requested = false;
    s_vad_tracker = (companion_audio_vad_tracker_t){0};
    s_have_upload_token = false;
    s_last_upload_token = (companion_audio_token_t){0};
    s_initialized = true;
    ESP_LOGI(TAG,
             "audio ready tdm=16000Hz/4slot afe=MMR mic=[0,2] ref=[1]*4 ns=1 aec=tts-only mode=%d vad=1 wakenet=1 opus_tx=16000/mono/60ms/17000 tx_vbr=1 tx_dtx=1 opus_rx=24000/mono/60ms play=16000 volume=%u playback_queue=32 pcm_queue=%u snapshot=%u",
             COMPANION_AUDIO_AEC_MODE_VOIP_LOW_COST,
             COMPANION_AUDIO_OUTPUT_VOLUME,
             COMPANION_AUDIO_PCM_QUEUE_DEPTH,
             COMPANION_AUDIO_SNAPSHOT_FRAMES);
    return ESP_OK;
}

esp_err_t companion_audio_start(void)
{
    const companion_audio_task_policy_t task_policy =
        companion_audio_make_task_policy();
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (!s_initialized || s_fatal_reported || s_starting) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_running) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_OK;
    }
    if (NULL != s_playback_task || NULL != s_feed_task ||
        NULL != s_fetch_task || NULL != s_encode_task) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_starting = true;
    s_stop_requested = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (NULL == s_playback_queue) {
        s_playback_queue = xQueueCreate(COMPANION_AUDIO_PLAYBACK_QUEUE_DEPTH,
                                        sizeof(playback_item_t));
    }
    if (NULL == s_playback_queue) {
        portENTER_CRITICAL(&s_lifecycle_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_NO_MEM;
    }
    drain_playback_queue();
    clear_pcm_queue();
    (void)xEventGroupClearBits(s_task_events, AUDIO_READY_BITS |
                                              AUDIO_EXIT_BITS);
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (s_stop_requested) {
        s_starting = false;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_running = true;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    BaseType_t play_result = xTaskCreatePinnedToCoreWithCaps(
        playback_task_fn, "companion_play", PLAYBACK_TASK_STACK, NULL,
        task_policy.playback_priority, &s_playback_task,
        task_policy.playback_core,
        MALLOC_CAP_SPIRAM);
    BaseType_t feed_result = xTaskCreatePinnedToCoreWithCaps(
        feed_task, "companion_feed", AUDIO_TASK_STACK, NULL,
        task_policy.feed_priority, &s_feed_task, task_policy.feed_core,
        MALLOC_CAP_SPIRAM);
    BaseType_t fetch_result = xTaskCreatePinnedToCoreWithCaps(
        fetch_task, "companion_fetch", AUDIO_TASK_STACK, NULL,
        task_policy.fetch_priority, &s_fetch_task, task_policy.fetch_core,
        MALLOC_CAP_SPIRAM);
    BaseType_t encode_result = xTaskCreatePinnedToCoreWithCaps(
        encode_task, "companion_encode", AUDIO_TASK_STACK, NULL,
        task_policy.encode_priority, &s_encode_task,
        task_policy.encode_core, MALLOC_CAP_SPIRAM);
    bool stop_requested = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_starting = false;
    stop_requested = s_stop_requested;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (stop_requested) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pdPASS != play_result || pdPASS != feed_result ||
        pdPASS != fetch_result || pdPASS != encode_result) {
        stop_for_fatal_error(ESP_FAIL);
        (void)companion_audio_stop_ex(AUDIO_TASK_STOP_TIMEOUT_MS);
        return ESP_FAIL;
    }
    const EventBits_t ready = xEventGroupWaitBits(
        s_task_events, AUDIO_READY_BITS, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(AUDIO_TASK_READY_TIMEOUT_MS));
    if (AUDIO_READY_BITS != (ready & AUDIO_READY_BITS) || !s_running) {
        stop_for_fatal_error((ESP_OK != s_fatal_error) ? s_fatal_error :
                                                          ESP_ERR_TIMEOUT);
        (void)companion_audio_stop_ex(AUDIO_TASK_STOP_TIMEOUT_MS);
        return (ESP_OK != s_fatal_error) ? s_fatal_error : ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG,
             "audio tasks started; cores feed=%u fetch=%u playback=%u encode=%u priorities feed=%u fetch=%u playback=%u encode=%u yield_after_feed=1tick TDM reader count=1",
             task_policy.feed_core, task_policy.fetch_core,
             task_policy.playback_core, task_policy.encode_core,
             task_policy.feed_priority, task_policy.fetch_priority,
             task_policy.playback_priority, task_policy.encode_priority);
    return ESP_OK;
}

esp_err_t companion_audio_stop(void)
{
    return companion_audio_stop_ex(AUDIO_TASK_STOP_TIMEOUT_MS);
}

esp_err_t companion_audio_stop_ex(uint32_t timeout_ms)
{
    if (0U == timeout_ms || NULL == s_task_events) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_stop_requested = true;
    s_running = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    const esp_err_t output_stop_result = companion_audio_play_stop();
    const TickType_t start_tick = xTaskGetTickCount();
    while (true) {
        bool active = false;
        portENTER_CRITICAL(&s_lifecycle_lock);
        active = s_starting || NULL != s_playback_task ||
                 NULL != s_feed_task || NULL != s_fetch_task ||
                 NULL != s_encode_task;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        if (!active) {
            break;
        }
        if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(timeout_ms)) {
            ESP_LOGE(TAG, "audio task group stop timeout after %lums",
                     (unsigned long)timeout_ms);
            stop_for_fatal_error(ESP_ERR_TIMEOUT);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    drain_playback_queue();
    clear_pcm_queue();
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_vad_tracker = (companion_audio_vad_tracker_t){0};
    s_was_vad_active = false;
    s_stop_requested = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return output_stop_result;
}

esp_err_t companion_audio_copy_snapshot(uint32_t wake_seq, int16_t *mic1,
                                        int16_t *mic2, size_t frame_capacity,
                                        uint32_t *snapshot_version)
{
    if (0U == wake_seq || NULL == mic1 || NULL == mic2 ||
        NULL == snapshot_version ||
        frame_capacity < COMPANION_AUDIO_SNAPSHOT_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (NULL == s_snapshot_lock ||
        pdTRUE != xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    if (wake_seq != s_frozen_wake_seq) {
        result = ESP_ERR_NOT_FOUND;
    } else {
        const size_t bytes = COMPANION_AUDIO_SNAPSHOT_FRAMES * sizeof(int16_t);
        memcpy(mic1, s_frozen_mic1, bytes);
        memcpy(mic2, s_frozen_mic2, bytes);
        *snapshot_version = s_stats.snapshot_version;
    }
    xSemaphoreGive(s_snapshot_lock);
    return result;
}

esp_err_t companion_audio_play_opus(const uint8_t *data, int length)
{
    companion_audio_token_t token = {0};
    if (NULL != s_output_lock &&
        pdTRUE == xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(100))) {
        token = s_output_token;
        xSemaphoreGive(s_output_lock);
    }
    return companion_audio_play_opus_ex(data, length, &token);
}

esp_err_t companion_audio_play_opus_ex(const uint8_t *data, int length,
                                       const companion_audio_token_t *token)
{
    if (NULL == data || 0 >= length || !token_valid(token)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!audio_runtime_accepts_output() || NULL == s_playback_queue ||
        NULL == s_output_device || NULL == s_output_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    companion_audio_output_owner_t owner = COMPANION_AUDIO_OUTPUT_SILENT;
    if (pdTRUE != xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }
    owner = s_output_owner;
    const bool prompt_active = COMPANION_AUDIO_OUTPUT_PROMPT == owner;
    xSemaphoreGive(s_output_lock);
    if (prompt_active) {
        const esp_err_t stop_result = companion_audio_play_stop();
        if (ESP_OK != stop_result) {
            return stop_result;
        }
    }
    if (pdTRUE != xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }
    if (COMPANION_AUDIO_OUTPUT_STOPPING == s_output_phase ||
        (COMPANION_AUDIO_OUTPUT_TTS == s_output_owner &&
         !token_matches(token, &s_output_token)) ||
        (COMPANION_AUDIO_OUTPUT_SILENT != s_output_owner &&
         COMPANION_AUDIO_OUTPUT_TTS != s_output_owner)) {
        xSemaphoreGive(s_output_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const bool starting_tts = COMPANION_AUDIO_OUTPUT_TTS != s_output_owner;
    if (starting_tts) {
        const esp_err_t aec_result = set_tts_aec_enabled_locked(true);
        if (ESP_OK != aec_result) {
            xSemaphoreGive(s_output_lock);
            return aec_result;
        }
    }
    s_output_owner = COMPANION_AUDIO_OUTPUT_TTS;
    s_output_phase = COMPANION_AUDIO_OUTPUT_ACTIVE;
    s_output_token = *token;
    if (starting_tts) {
        log_aec_state_locked("tts_start_aec_on");
    }
    (void)esp_codec_dev_set_out_vol(s_output_device,
                                    COMPANION_AUDIO_OUTPUT_VOLUME);
    xSemaphoreGive(s_output_lock);
    uint8_t *copy = heap_caps_malloc((size_t)length, MALLOC_CAP_SPIRAM);
    if (NULL == copy) {
        rollback_tts_output(token);
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, data, (size_t)length);
    const playback_item_t item = {
        .data = copy,
        .length = length,
        .token = *token,
    };
    if (pdTRUE != xQueueSend(s_playback_queue, &item, 0)) {
        heap_caps_free(copy);
        rollback_tts_output(token);
        s_stats.playback_drops++;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t stop_output(const companion_audio_token_t *required_token)
{
    if (NULL == s_output_lock ||
        pdTRUE != xSemaphoreTake(s_output_lock,
                                 pdMS_TO_TICKS(AUDIO_OUTPUT_STOP_TIMEOUT_MS))) {
        return ESP_ERR_TIMEOUT;
    }
    if (COMPANION_AUDIO_OUTPUT_SILENT == s_output_owner) {
        s_output_phase = COMPANION_AUDIO_OUTPUT_IDLE;
        s_output_token = (companion_audio_token_t){0};
        const esp_err_t aec_result = set_tts_aec_enabled_locked(false);
        log_aec_state_locked("silent_stop_aec_off");
        xSemaphoreGive(s_output_lock);
        return aec_result;
    }
    if (COMPANION_AUDIO_OUTPUT_STOPPING == s_output_phase) {
        xSemaphoreGive(s_output_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL != required_token &&
        !token_matches(required_token, &s_output_token)) {
        xSemaphoreGive(s_output_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const companion_audio_output_owner_t owner = s_output_owner;
    const companion_audio_token_t token = s_output_token;
    s_output_phase = COMPANION_AUDIO_OUTPUT_STOPPING;
    xSemaphoreGive(s_output_lock);

    esp_err_t stop_result = ESP_OK;
    if (COMPANION_AUDIO_OUTPUT_PROMPT == owner) {
        if (NULL == s_prompt_player || NULL == s_prompt_lock ||
            NULL == s_prompt_terminal) {
            stop_result = ESP_ERR_INVALID_STATE;
        } else if (pdTRUE != xSemaphoreTake(
                       s_prompt_lock,
                       pdMS_TO_TICKS(AUDIO_OUTPUT_STOP_TIMEOUT_MS))) {
            stop_result = ESP_ERR_TIMEOUT;
        } else {
            if (PROMPT_STATE_PLAYING == s_prompt_state) {
                while (pdTRUE == xSemaphoreTake(s_prompt_terminal, 0U)) {
                }
                prompt_write_silence();
                esp_audio_simple_player_stop(s_prompt_player);
                if (pdTRUE != xSemaphoreTake(
                        s_prompt_terminal,
                        pdMS_TO_TICKS(AUDIO_OUTPUT_STOP_TIMEOUT_MS))) {
                    stop_result = ESP_ERR_TIMEOUT;
                }
            }
            if (ESP_OK == stop_result) {
                s_prompt_state = PROMPT_STATE_IDLE;
                s_prompt_fade_remaining = 0;
            }
            xSemaphoreGive(s_prompt_lock);
        }
    }
    drain_playback_queue();
    if (pdTRUE != xSemaphoreTake(
            s_output_lock, pdMS_TO_TICKS(AUDIO_OUTPUT_STOP_TIMEOUT_MS))) {
        stop_for_fatal_error(ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }
    if (COMPANION_AUDIO_OUTPUT_TTS == owner) {
        const esp_err_t aec_result = set_tts_aec_enabled_locked(false);
        if (ESP_OK != aec_result) {
            stop_result = aec_result;
        }
        log_aec_state_locked("tts_stop_aec_off");
    }
    if (ESP_OK == stop_result &&
        (output_identity_matches(owner, &token) ||
         COMPANION_AUDIO_OUTPUT_SILENT == s_output_owner)) {
        s_output_owner = COMPANION_AUDIO_OUTPUT_SILENT;
        s_output_phase = COMPANION_AUDIO_OUTPUT_IDLE;
        s_output_token = (companion_audio_token_t){0};
    }
    xSemaphoreGive(s_output_lock);
    if (ESP_OK != stop_result) {
        stop_for_fatal_error(stop_result);
        ESP_LOGE(TAG,
                 "audio output stop unconfirmed owner=%d token=%lu/%lu/%lu/%lu error=%s",
                 (int)owner, (unsigned long)token.generation,
                 (unsigned long)token.wake_seq,
                 (unsigned long)token.session_epoch,
                 (unsigned long)token.request_id,
                 esp_err_to_name(stop_result));
        return stop_result;
    }
    if (s_initialized && COMPANION_AUDIO_OUTPUT_PROMPT != owner) {
        (void)audio_processor_reset_buffer();
    }
    if (COMPANION_AUDIO_OUTPUT_PROMPT != owner) {
        s_was_vad_active = false;
    }
    ESP_LOGI(TAG, "audio output stopped owner=%d result=%s",
             (int)owner, esp_err_to_name(stop_result));
    return stop_result;
}

esp_err_t companion_audio_play_stop(void)
{
    return stop_output(NULL);
}

esp_err_t companion_audio_play_stop_ex(const companion_audio_token_t *token)
{
    if (!token_valid(token)) {
        return ESP_ERR_INVALID_ARG;
    }
    return stop_output(token);
}

esp_err_t companion_audio_prompt_play(const char *url)
{
    const companion_audio_token_t legacy = {
        .generation = 1U,
        .wake_seq = 1U,
        .request_id = 1U,
    };
    return companion_audio_prompt_play_ex(url, &legacy);
}

esp_err_t companion_audio_prompt_play_ex(
    const char *url, const companion_audio_token_t *token)
{
    if (NULL == url || !token_valid(token)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bind_vad_prompt_gate(token)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!audio_runtime_accepts_output() || NULL == s_prompt_player ||
        NULL == s_prompt_lock || NULL == s_prompt_terminal ||
        NULL == s_output_lock || !s_prompt_ready) {
        (void)request_vad_prompt_reset(token);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t stop_result = companion_audio_play_stop();
    if (ESP_OK != stop_result) {
        (void)request_vad_prompt_reset(token);
        return stop_result;
    }
    if (pdTRUE != xSemaphoreTake(s_prompt_lock, pdMS_TO_TICKS(1000))) {
        (void)request_vad_prompt_reset(token);
        return ESP_ERR_TIMEOUT;
    }
    if (pdTRUE != xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(1000))) {
        xSemaphoreGive(s_prompt_lock);
        (void)request_vad_prompt_reset(token);
        return ESP_ERR_TIMEOUT;
    }
    if (COMPANION_AUDIO_OUTPUT_SILENT != s_output_owner ||
        COMPANION_AUDIO_OUTPUT_IDLE != s_output_phase) {
        xSemaphoreGive(s_output_lock);
        xSemaphoreGive(s_prompt_lock);
        (void)request_vad_prompt_reset(token);
        return ESP_ERR_INVALID_STATE;
    }
    (void)esp_codec_dev_set_out_vol(s_output_device,
                                    COMPANION_AUDIO_OUTPUT_VOLUME);
    prompt_write_silence();
    s_output_owner = COMPANION_AUDIO_OUTPUT_PROMPT;
    s_output_phase = COMPANION_AUDIO_OUTPUT_ACTIVE;
    s_output_token = *token;
    s_prompt_callback_token = *token;
    xSemaphoreGive(s_output_lock);
    while (NULL != s_prompt_terminal &&
           pdTRUE == xSemaphoreTake(s_prompt_terminal, 0U)) {
    }
    s_prompt_state = PROMPT_STATE_PLAYING;
    s_prompt_fade_remaining = PROMPT_FADE_IN_SAMPLES;
    esp_err_t result = esp_audio_simple_player_run(s_prompt_player, url, NULL);
    ESP_LOGI(TAG,
             "[DEBUG-WSRAM] stage=prompt_start result=%s internal_free=%lu internal_largest=%lu internal_min=%lu psram_free=%lu",
             esp_err_to_name(result),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_minimum_free_size(
                 MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (ESP_OK != result) {
        s_prompt_state = PROMPT_STATE_IDLE;
        s_prompt_fade_remaining = 0;
        bool owner_closed = false;
        if (pdTRUE == xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(1000))) {
            if (output_identity_matches(COMPANION_AUDIO_OUTPUT_PROMPT,
                                        token)) {
                s_output_owner = COMPANION_AUDIO_OUTPUT_SILENT;
                s_output_phase = COMPANION_AUDIO_OUTPUT_IDLE;
                s_output_token = (companion_audio_token_t){0};
                s_prompt_callback_token = (companion_audio_token_t){0};
            }
            xSemaphoreGive(s_output_lock);
            owner_closed = true;
        }
        if (!owner_closed) {
            stop_for_fatal_error(ESP_ERR_TIMEOUT);
            result = ESP_ERR_TIMEOUT;
        }
        (void)request_vad_prompt_reset(token);
    }
    xSemaphoreGive(s_prompt_lock);
    return result;
}

esp_err_t companion_audio_prompt_stop(void)
{
    return companion_audio_play_stop();
}

void companion_audio_get_stats(companion_audio_stats_t *stats)
{
    if (NULL != stats) {
        *stats = s_stats;
        if (NULL != s_output_lock &&
            pdTRUE == xSemaphoreTake(s_output_lock, pdMS_TO_TICKS(20))) {
            stats->output_owner = s_output_owner;
            stats->output_phase = s_output_phase;
            stats->output_token = s_output_token;
            xSemaphoreGive(s_output_lock);
        }
        if (NULL != s_pcm_queue_lock &&
            pdTRUE == xSemaphoreTake(s_pcm_queue_lock, pdMS_TO_TICKS(20))) {
            stats->pcm_queue_depth =
                (uint32_t)companion_audio_pcm_queue_count(&s_pcm_queue);
            stats->pcm_queue_high_water = (uint32_t)s_pcm_queue.high_water;
            xSemaphoreGive(s_pcm_queue_lock);
        }
    }
}
