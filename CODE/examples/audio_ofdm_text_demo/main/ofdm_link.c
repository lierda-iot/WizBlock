#include "ofdm_link.h"

#include "ofdm_audio.h"
#include "ofdm_calibration.h"
#include "ofdm_fec.h"
#include "ofdm_frame.h"
#include "ofdm_phy.h"
#include "ofdm_sync.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OFDM_LINK_AUDIO_QUEUE_LENGTH 80U
#define OFDM_LINK_COMMAND_QUEUE_LENGTH 4U
#define OFDM_LINK_SEARCH_SAMPLES 4096U
#define OFDM_LINK_ROUND_UP(value, alignment) \
    ((((value) + (alignment) - 1U) / (alignment)) * (alignment))
#define OFDM_LINK_RETAIN_SAMPLES \
    OFDM_LINK_ROUND_UP(OFDM_FRAME_SAMPLE_COUNT + \
                           OFDM_SYNC_TIMING_SEARCH_SAMPLES, \
                       OFDM_AUDIO_READ_FRAMES)
#define OFDM_LINK_CAPTURE_SAMPLES \
    (OFDM_LINK_RETAIN_SAMPLES + OFDM_LINK_SEARCH_SAMPLES)
#define OFDM_LINK_AUDIO_TASK_STACK 4096U
#define OFDM_LINK_MODEM_TASK_STACK 16384U
#define OFDM_LINK_AUDIO_TASK_PRIORITY 9U
#define OFDM_LINK_MODEM_TASK_PRIORITY 8U
#define OFDM_LINK_TASK_CORE 0
#define OFDM_LINK_AUDIO_TIMEOUT_MS 100U
#define OFDM_LINK_MODEM_WAIT_MS 20U
#define OFDM_LINK_MODEM_YIELD_TICKS 1U
#define OFDM_LINK_TX_AMP_SETTLE_MS 100U
#define OFDM_LINK_TX_GUARD_MS 120U
#define OFDM_LINK_RESULT_HOLD_US INT64_C(600000)
#define OFDM_LINK_SESSION_TIMEOUT_US INT64_C(5000000)
#define OFDM_LINK_DEFAULT_PCM_SCALE ((float)OFDM_NORMAL_PCM_SCALE)
#define OFDM_LINK_PCM_MAX 32767.0F
#define OFDM_LINK_PCM_MIN -32768.0F
#define OFDM_LINK_PERCENT_MAX 100U
#define OFDM_LINK_RX_CLIP_THRESHOLD 32760U
#define OFDM_LINK_CHIRP_SCORE_SCALE 1000.0F
#define OFDM_LINK_CAL_SAFE_TX_PROFILE OFDM_CAL_SAFE_TX_PROFILE_INDEX
#define OFDM_LINK_CAL_DEFAULT_RX_GAIN OFDM_CAL_DEFAULT_RX_GAIN_INDEX
#define OFDM_LINK_CAL_TOTAL_FRAMES OFDM_CAL_TOTAL_FRAME_COUNT
#define OFDM_LINK_CAL_TIMEOUT_US \
    ((int64_t)OFDM_CAL_TIMEOUT_MS * INT64_C(1000))
#define OFDM_LINK_CAL_RESULT_HOLD_US INT64_C(1500000)
#define OFDM_LINK_CAL_TAIL_GUARD_US INT64_C(3500000)
#define OFDM_LINK_CAL_END_WAIT_US INT64_C(8000000)
#define OFDM_LINK_CAL_FRAME_SETTLE_MS 400U
#define OFDM_LINK_CAL_GAIN_SETTLE_MS 250U
#define OFDM_LINK_DSP_STALL_BUDGET_MS 400U
#define OFDM_LINK_AUDIO_QUEUE_COVERAGE_MS \
    ((OFDM_LINK_AUDIO_QUEUE_LENGTH * OFDM_AUDIO_READ_FRAMES * 1000U) / \
     OFDM_SAMPLE_RATE_HZ)

_Static_assert(OFDM_LINK_AUDIO_QUEUE_LENGTH * OFDM_AUDIO_READ_FRAMES *
                       1000U >=
                   OFDM_LINK_DSP_STALL_BUDGET_MS * OFDM_SAMPLE_RATE_HZ,
               "audio queue must cover the measured DSP stall budget");
_Static_assert(OFDM_CAL_END_TRANSMIT_COUNT >= 2U,
               "calibration END must have transport redundancy");
_Static_assert(OFDM_CAL_START_TRANSMIT_COUNT >= 2U,
               "calibration START must have transport redundancy");

typedef struct {
    int16_t samples[OFDM_AUDIO_READ_FRAMES];
} ofdm_audio_block_t;

typedef enum {
    OFDM_LINK_COMMAND_SEND = 1,
    OFDM_LINK_COMMAND_TX_CALIBRATION = 2,
    OFDM_LINK_COMMAND_RX_CALIBRATION = 3,
    OFDM_LINK_COMMAND_STOP_CALIBRATION = 4,
} ofdm_link_command_t;

static const char *TAG = "ofdm_link";
static const uint8_t s_test_message[] =
    "你好，这是 ESP32-S3 OFDM 声学文字链路测试。"
    "发送端只确认声波已发出，接收端通过 CRC 后才显示完整文字。";

static QueueHandle_t s_audio_queue;
static QueueHandle_t s_command_queue;
static SemaphoreHandle_t s_snapshot_mutex;
static TaskHandle_t s_audio_task_handle;
static TaskHandle_t s_modem_task_handle;
static int16_t *s_capture_pcm;
static float *s_capture_float;
static float *s_frame_float;
static int16_t *s_transmit_pcm;
static ofdm_link_snapshot_t s_snapshot;
static ofdm_link_health_t s_health;
static ofdm_reassembly_t s_reassembly;
static size_t s_capture_count;
static size_t s_next_search_offset;
static int64_t s_last_valid_frame_us;
static int64_t s_return_idle_at_us;
static volatile bool s_discard_receive;
static volatile bool s_calibration_cancel_requested;
static volatile bool s_calibration_rx_armed;
static volatile bool s_calibration_rx_active;
static volatile bool s_calibration_tx_active;
static volatile bool s_calibration_command_pending;
static volatile bool s_calibration_tail_guard;
static int64_t s_calibration_deadline_us;
static int64_t s_calibration_tail_guard_until_us;
static ofdm_calibration_stats_t s_calibration_stats;
static uint16_t s_calibration_run_id;
static uint16_t s_calibration_next_sequence;
static uint8_t s_calibration_expected_tx;
static uint8_t s_calibration_expected_rx;
static bool s_calibration_control_ready;
static float s_pcm_scale = OFDM_LINK_DEFAULT_PCM_SCALE;
static portMUX_TYPE s_health_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;

static void refresh_calibration_deadline(void)
{
    const int64_t wait_us =
        OFDM_LINK_CAL_TOTAL_FRAMES - 1U == s_calibration_next_sequence
            ? OFDM_LINK_CAL_END_WAIT_US
            : OFDM_LINK_CAL_TIMEOUT_US;
    s_calibration_deadline_us = esp_timer_get_time() + wait_us;
}

static void publish_calibration_progress(ofdm_link_state_t state,
                                         uint16_t sequence,
                                         const char *status);
static void record_calibration_sample(
    uint16_t sequence,
    bool phy_ok,
    bool crc_ok,
    uint32_t peak,
    uint32_t clip_samples,
    const ofdm_phy_frame_metrics_t *metrics,
    const char *reason);

static void increment_counter(uint32_t *counter)
{
    portENTER_CRITICAL(&s_health_lock);
    if (UINT32_MAX > *counter) {
        ++(*counter);
    }
    portEXIT_CRITICAL(&s_health_lock);
}

static void add_counter(uint32_t *counter, uint32_t increment)
{
    portENTER_CRITICAL(&s_health_lock);
    if (UINT32_MAX - *counter < increment) {
        *counter = UINT32_MAX;
    } else {
        *counter += increment;
    }
    portEXIT_CRITICAL(&s_health_lock);
}

static void update_dsp_max(uint32_t elapsed_us)
{
    portENTER_CRITICAL(&s_health_lock);
    if (s_health.dsp_us_max < elapsed_us) {
        s_health.dsp_us_max = elapsed_us;
    }
    portEXIT_CRITICAL(&s_health_lock);
}

static void update_rx_queue_peak(uint32_t queue_depth)
{
    portENTER_CRITICAL(&s_health_lock);
    if (s_health.rx_queue_peak < queue_depth) {
        s_health.rx_queue_peak = queue_depth;
    }
    portEXIT_CRITICAL(&s_health_lock);
}

static void update_rx_activity(const int16_t *samples, size_t sample_count)
{
    if (NULL == samples || 0U == sample_count) {
        return;
    }
    uint64_t square_sum = 0U;
    uint32_t peak = 0U;
    uint32_t clipped_samples = 0U;
    for (size_t index = 0U; index < sample_count; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = (uint32_t)(0 > sample ? -sample : sample);
        if (peak < magnitude) {
            peak = magnitude;
        }
        if (OFDM_LINK_RX_CLIP_THRESHOLD <= magnitude) {
            ++clipped_samples;
        }
        square_sum += (uint64_t)((int64_t)sample * (int64_t)sample);
    }
    const uint32_t mean_square = (uint32_t)(square_sum / sample_count);

    portENTER_CRITICAL(&s_health_lock);
    if (s_health.rx_peak_max < peak) {
        s_health.rx_peak_max = peak;
    }
    if (s_health.rx_mean_square_max < mean_square) {
        s_health.rx_mean_square_max = mean_square;
    }
    if (UINT32_MAX - s_health.rx_clip_samples < clipped_samples) {
        s_health.rx_clip_samples = UINT32_MAX;
    } else {
        s_health.rx_clip_samples += clipped_samples;
    }
    portEXIT_CRITICAL(&s_health_lock);
}

static void update_chirp_activity(float best_score, bool detected)
{
    if (!isfinite(best_score) || 0.0F > best_score) {
        return;
    }
    const float bounded_score = 1.0F < best_score ? 1.0F : best_score;
    const uint16_t score_milli = (uint16_t)lrintf(
        bounded_score * OFDM_LINK_CHIRP_SCORE_SCALE);

    portENTER_CRITICAL(&s_health_lock);
    if (s_health.chirp_score_max_milli < score_milli) {
        s_health.chirp_score_max_milli = score_milli;
    }
    if (detected && UINT32_MAX > s_health.chirp_hits) {
        ++s_health.chirp_hits;
    }
    portEXIT_CRITICAL(&s_health_lock);
}

static void publish_snapshot(ofdm_link_state_t state,
                             uint16_t session_id,
                             uint8_t frame_index,
                             uint8_t frame_count,
                             uint16_t received_bitmap,
                             uint16_t message_bytes,
                             bool audio_sent,
                             const char *status,
                             const char *message)
{
    if (NULL == s_snapshot_mutex) {
        return;
    }
    if (pdTRUE != xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100U))) {
        return;
    }
    s_snapshot.state = state;
    s_snapshot.session_id = session_id;
    s_snapshot.frame_index = frame_index;
    s_snapshot.frame_count = frame_count;
    s_snapshot.received_bitmap = received_bitmap;
    s_snapshot.message_bytes = message_bytes;
    s_snapshot.audio_sent = audio_sent;
    s_snapshot.progress_percent = 0U == frame_count
                                      ? 0U
                                      : (uint8_t)(((uint16_t)frame_index *
                                                   OFDM_LINK_PERCENT_MAX) /
                                                  frame_count);
    if (NULL != status) {
        (void)snprintf(s_snapshot.status, sizeof(s_snapshot.status), "%s",
                       status);
    }
    if (NULL != message) {
        (void)snprintf(s_snapshot.message, sizeof(s_snapshot.message), "%s",
                       message);
    }
    xSemaphoreGive(s_snapshot_mutex);
}

static bool is_transient_result_state(ofdm_link_state_t state)
{
    return OFDM_LINK_STATE_RX_OK == state ||
           OFDM_LINK_STATE_RX_ERROR == state ||
           OFDM_LINK_STATE_TX_DONE == state ||
           OFDM_LINK_STATE_ERROR == state;
}

static void schedule_idle_return(void)
{
    s_return_idle_at_us = esp_timer_get_time() + OFDM_LINK_RESULT_HOLD_US;
}

static void return_to_idle_if_due(void)
{
    if (0 == s_return_idle_at_us ||
        esp_timer_get_time() < s_return_idle_at_us) {
        return;
    }
    ofdm_link_snapshot_t snapshot = {0};
    if (ofdm_link_get_snapshot(&snapshot) &&
        (s_calibration_tail_guard ||
         is_transient_result_state(snapshot.state))) {
        if (s_calibration_tail_guard) {
            publish_snapshot(OFDM_LINK_STATE_IDLE_RX, 0U, 0U, 0U, 0U,
                             0U, false, "监听声学链路", "");
        } else {
            const bool received_message =
                OFDM_LINK_STATE_RX_OK == snapshot.state &&
                '\0' != snapshot.message[0];
            publish_snapshot(
                OFDM_LINK_STATE_IDLE_RX, 0U, 0U, 0U, 0U,
                received_message ? snapshot.message_bytes : 0U, false,
                received_message ? "上次接收成功，继续监听" : "监听声学链路",
                received_message ? NULL : "");
        }
    }
    s_return_idle_at_us = 0;
}

static void restore_receive_state_after_candidate_failure(void)
{
    if (s_calibration_rx_armed || s_calibration_rx_active) {
        publish_calibration_progress(
            OFDM_LINK_STATE_RX_DATA, s_calibration_next_sequence,
            s_calibration_rx_active ? "校准接收中，等待下一帧"
                                    : "校准接收待命，等待发送端");
        ESP_LOGI(TAG, "OFDM_CAL role=RX stage=FRAME result=RETRY next=%u",
                 (unsigned int)s_calibration_next_sequence);
        return;
    }
    if (s_reassembly.active) {
        const uint8_t received_count =
            ofdm_reassembly_received_count(&s_reassembly);
        publish_snapshot(OFDM_LINK_STATE_RX_DATA, s_reassembly.session_id,
                         received_count, s_reassembly.frame_count,
                         s_reassembly.received_bitmap,
                         s_reassembly.message_length, false,
                         "正在接收数据", NULL);
        ESP_LOGI(TAG,
                 "OFDM_RX candidate=REJECTED next=RX_DATA received=%u/%u",
                 (unsigned int)received_count,
                 (unsigned int)s_reassembly.frame_count);
        return;
    }

    publish_snapshot(OFDM_LINK_STATE_IDLE_RX, 0U, 0U, 0U, 0U,
                     0U, false,
                     "监听声学链路", NULL);
    ESP_LOGI(TAG, "OFDM_RX candidate=REJECTED next=IDLE received=0/0");
}

static void clear_audio_queue(void)
{
    ofdm_audio_block_t block = {0};
    while (pdTRUE == xQueueReceive(s_audio_queue, &block, 0U)) {
    }
}

static void reset_capture(void)
{
    s_capture_count = 0U;
    s_next_search_offset = 0U;
}

static uint16_t create_session_id(void)
{
    uint16_t session_id = (uint16_t)esp_random();
    return 0U == session_id ? 1U : session_id;
}

static int16_t float_to_pcm(float sample, uint32_t *clip_count)
{
    float scaled = sample * s_pcm_scale;
    if (OFDM_LINK_PCM_MAX < scaled) {
        scaled = OFDM_LINK_PCM_MAX;
        if (NULL != clip_count && UINT32_MAX > *clip_count) {
            ++(*clip_count);
        }
    } else if (OFDM_LINK_PCM_MIN > scaled) {
        scaled = OFDM_LINK_PCM_MIN;
        if (NULL != clip_count && UINT32_MAX > *clip_count) {
            ++(*clip_count);
        }
    }
    return (int16_t)lrintf(scaled);
}

static esp_err_t apply_tx_profile(uint8_t profile_index)
{
    const ofdm_calibration_tx_profile_t *profile =
        ofdm_calibration_get_tx_profile(profile_index);
    if (NULL == profile) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = ofdm_audio_set_output_volume(
        profile->volume_percent);
    if (ESP_OK == result) {
        s_pcm_scale = (float)profile->pcm_scale;
    }
    return result;
}

static esp_err_t apply_default_audio_profile(void)
{
    esp_err_t result = ofdm_audio_set_output_volume(
        OFDM_AUDIO_DEFAULT_OUTPUT_VOLUME);
    if (ESP_OK == result) {
        result = ofdm_audio_set_input_gain(
            OFDM_AUDIO_DEFAULT_INPUT_GAIN_DB);
    }
    s_pcm_scale = OFDM_LINK_DEFAULT_PCM_SCALE;
    return result;
}

static esp_err_t apply_rx_gain_index(uint8_t gain_index)
{
    float gain_db = 0.0F;
    if (!ofdm_calibration_get_rx_gain(gain_index, &gain_db)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ofdm_audio_set_input_gain(gain_db);
}

static void measure_pcm_frame(const int16_t *samples,
                              size_t sample_count,
                              uint32_t *peak,
                              uint32_t *clip_samples)
{
    if (NULL == peak || NULL == clip_samples) {
        return;
    }
    *peak = 0U;
    *clip_samples = 0U;
    if (NULL == samples) {
        return;
    }
    for (size_t index = 0U; index < sample_count; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = (uint32_t)(0 > sample ? -sample : sample);
        if (*peak < magnitude) {
            *peak = magnitude;
        }
        if (OFDM_LINK_RX_CLIP_THRESHOLD <= magnitude) {
            if (UINT32_MAX > *clip_samples) {
                ++(*clip_samples);
            }
        }
    }
}

static void log_calibration_sample(uint16_t sequence,
                                   uint8_t tx_profile,
                                   uint8_t rx_gain_index,
                                   bool phy_ok,
                                   bool crc_ok,
                                   uint32_t peak,
                                   uint32_t clip_samples,
                                   const ofdm_phy_frame_metrics_t *metrics,
                                   const char *reason)
{
    float gain_db = -1.0F;
    (void)ofdm_calibration_get_rx_gain(rx_gain_index, &gain_db);
    const float header_evm_db = NULL != metrics
                                    ? metrics->header_evm_db
                                    : OFDM_PHY_RESPONSE_INVALID_DB;
    const float payload_evm_db = NULL != metrics
                                     ? metrics->payload_evm_db
                                     : OFDM_PHY_RESPONSE_INVALID_DB;
    const float band_0 = NULL != metrics
                             ? metrics->channel_group_db[0]
                             : OFDM_PHY_RESPONSE_INVALID_DB;
    const float band_1 = NULL != metrics
                             ? metrics->channel_group_db[1]
                             : OFDM_PHY_RESPONSE_INVALID_DB;
    const float band_2 = NULL != metrics
                             ? metrics->channel_group_db[2]
                             : OFDM_PHY_RESPONSE_INVALID_DB;
    const float band_3 = NULL != metrics
                             ? metrics->channel_group_db[3]
                             : OFDM_PHY_RESPONSE_INVALID_DB;
    const float band_4 = NULL != metrics
                             ? metrics->channel_group_db[4]
                             : OFDM_PHY_RESPONSE_INVALID_DB;
    const float band_5 = NULL != metrics
                             ? metrics->channel_group_db[5]
                             : OFDM_PHY_RESPONSE_INVALID_DB;
    const float band_6 = NULL != metrics
                             ? metrics->channel_group_db[6]
                             : OFDM_PHY_RESPONSE_INVALID_DB;
    const float band_7 = NULL != metrics
                             ? metrics->channel_group_db[7]
                             : OFDM_PHY_RESPONSE_INVALID_DB;
    ESP_LOGI(TAG,
             "OFDM_CAL role=RX stage=SAMPLE seq=%u tx=%u rx=%u gain=%.1f phy=%u crc=%u peak=%lu clip=%lu header_evm_db=%.1f payload_evm_db=%.1f bands=%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f reason=%s",
             (unsigned int)sequence, (unsigned int)tx_profile,
             (unsigned int)rx_gain_index, gain_db, phy_ok ? 1U : 0U,
             crc_ok ? 1U : 0U, (unsigned long)peak,
             (unsigned long)clip_samples, header_evm_db, payload_evm_db,
             band_0, band_1, band_2, band_3, band_4, band_5, band_6,
             band_7, NULL != reason ? reason : "UNKNOWN");
}

static void log_calibration_cells(void)
{
    for (uint8_t tx_index = 0U;
         tx_index < OFDM_CAL_TX_PROFILE_COUNT; ++tx_index) {
        for (uint8_t rx_index = 0U;
             rx_index < OFDM_CAL_RX_GAIN_COUNT; ++rx_index) {
            const ofdm_calibration_cell_t *cell =
                &s_calibration_stats.cells[tx_index][rx_index];
            ESP_LOGI(TAG,
                     "OFDM_CAL role=RX stage=CELL tx=%u rx=%u n=%lu phy=%.2f crc=%.2f peak=%lu clip=%lu evm_db=%.1f spread_db=%.1f",
                     (unsigned int)tx_index, (unsigned int)rx_index,
                     (unsigned long)cell->sample_count,
                     ofdm_calibration_cell_phy_rate(cell),
                     ofdm_calibration_cell_crc_rate(cell),
                     (unsigned long)cell->peak_max,
                     (unsigned long)cell->clip_samples,
                     ofdm_calibration_cell_payload_evm_db(cell),
                     ofdm_calibration_cell_band_spread_db(cell));
        }
    }
}

static void log_calibration_result(void)
{
    ofdm_calibration_recommendation_t recommendation = {0};
    log_calibration_cells();
    if (!ofdm_calibration_select_recommendation(&s_calibration_stats,
                                                &recommendation)) {
        ESP_LOGW(TAG, "OFDM_CAL role=RX stage=RESULT recommended=NONE");
        return;
    }
    const ofdm_calibration_tx_profile_t *profile =
        ofdm_calibration_get_tx_profile(recommendation.tx_profile);
    float gain_db = 0.0F;
    (void)ofdm_calibration_get_rx_gain(recommendation.rx_gain_index,
                                       &gain_db);
    ESP_LOGI(TAG,
             "OFDM_CAL role=RX stage=RESULT recommended_tx=%u volume=%u pcm=%u recommended_rx=%u gain=%.1f recommended_bins=%u..%u crc=%.2f phy=%.2f evm_db=%.1f spread_db=%.1f bands=%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
             (unsigned int)recommendation.tx_profile,
             NULL != profile ? (unsigned int)profile->volume_percent : 0U,
             NULL != profile ? (unsigned int)profile->pcm_scale : 0U,
             (unsigned int)recommendation.rx_gain_index, gain_db,
             (unsigned int)recommendation.first_bin,
             (unsigned int)recommendation.last_bin,
             recommendation.crc_success_rate,
             recommendation.phy_success_rate,
             recommendation.payload_evm_db,
             recommendation.band_spread_db,
             recommendation.band_db[0], recommendation.band_db[1],
             recommendation.band_db[2], recommendation.band_db[3],
             recommendation.band_db[4], recommendation.band_db[5],
             recommendation.band_db[6], recommendation.band_db[7]);
}

static const char *calibration_kind_name(uint8_t kind)
{
    switch (kind) {
        case OFDM_CAL_FRAME_START:
            return "START";
        case OFDM_CAL_FRAME_CONTROL:
            return "CONTROL";
        case OFDM_CAL_FRAME_SAMPLE:
            return "SAMPLE";
        case OFDM_CAL_FRAME_END:
            return "END";
        default:
            return "UNKNOWN";
    }
}

static bool calibration_should_abort(void)
{
    return s_calibration_cancel_requested ||
           (0 != s_calibration_deadline_us &&
            esp_timer_get_time() >= s_calibration_deadline_us);
}

static esp_err_t restore_default_audio_profile(void)
{
    const esp_err_t result = apply_default_audio_profile();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "OFDM_CAL stage=RESTORE result=FAIL error=%s",
                 esp_err_to_name(result));
    }
    return result;
}

static void reset_calibration_capture(void)
{
    ofdm_reassembly_reset(&s_reassembly);
    clear_audio_queue();
    reset_capture();
    s_last_valid_frame_us = esp_timer_get_time();
}

static void publish_calibration_progress(ofdm_link_state_t state,
                                         uint16_t sequence,
                                         const char *status)
{
    publish_snapshot(state, s_calibration_run_id, (uint8_t)(sequence + 1U),
                     (uint8_t)OFDM_LINK_CAL_TOTAL_FRAMES, 0U, 0U, false,
                     status, "");
}

static void finish_rx_calibration(bool success, const char *reason)
{
    const uint16_t run_id = s_calibration_run_id;
    const bool cancelled = s_calibration_cancel_requested;
    const bool hold_receive = !cancelled;
    s_calibration_tail_guard = hold_receive;
    s_calibration_tail_guard_until_us =
        hold_receive ? esp_timer_get_time() + OFDM_LINK_CAL_TAIL_GUARD_US
                     : 0;
    if (success) {
        log_calibration_result();
    }
    const esp_err_t restore_result = restore_default_audio_profile();
    const bool restore_ok = ESP_OK == restore_result;

    if (NULL != reason) {
        ESP_LOGI(TAG,
                 "OFDM_CAL role=RX stage=%s run=%u result=%s tail_guard_ms=%u",
                 success ? "RESULT" : "CANCEL", (unsigned int)run_id,
                 success && restore_ok ? "OK" : reason,
                 (unsigned int)(hold_receive
                                    ? OFDM_LINK_CAL_TAIL_GUARD_US / 1000
                                    : 0));
    }

    s_calibration_rx_active = false;
    s_calibration_rx_armed = false;
    s_calibration_deadline_us = 0;
    s_calibration_next_sequence = 0U;
    s_calibration_expected_tx = 0U;
    s_calibration_expected_rx = 0U;
    s_calibration_control_ready = false;
    s_calibration_cancel_requested = false;
    reset_calibration_capture();

    if (success && restore_ok) {
        publish_snapshot(OFDM_LINK_STATE_RX_OK, run_id, 0U, 0U, 0U, 0U,
                         false, "校准完成，建议仅在人工确认后采用", "");
        s_return_idle_at_us = esp_timer_get_time() +
                              OFDM_LINK_CAL_RESULT_HOLD_US;
    } else if (cancelled) {
        publish_snapshot(OFDM_LINK_STATE_IDLE_RX, 0U, 0U, 0U, 0U, 0U,
                         false, "已取消校准，继续监听", "");
        s_return_idle_at_us = 0;
        s_calibration_tail_guard = false;
        s_calibration_tail_guard_until_us = 0;
    } else {
        publish_snapshot(OFDM_LINK_STATE_RX_ERROR, run_id, 0U, 0U, 0U, 0U,
                         false, "校准未完成，已恢复监听", "");
        s_return_idle_at_us = esp_timer_get_time() +
                              OFDM_LINK_CAL_RESULT_HOLD_US;
    }
}

static esp_err_t transmit_calibration_frame(
    const ofdm_calibration_frame_t *calibration_frame,
    uint8_t waveform_profile,
    uint8_t physical_attempt,
    uint8_t physical_total)
{
    if (NULL == calibration_frame ||
        OFDM_CAL_TX_PROFILE_COUNT <= waveform_profile ||
        0U == physical_attempt || 0U == physical_total ||
        physical_attempt > physical_total) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = apply_tx_profile(waveform_profile);
    ofdm_frame_t frame = {0};
    uint8_t calibration_payload[OFDM_CAL_PAYLOAD_BYTES] = {0};
    uint8_t header_wire[OFDM_FRAME_HEADER_BYTES] = {0};
    uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint32_t clip_count = 0U;
    const uint16_t session_id = ofdm_calibration_make_session_id(
        calibration_frame->kind, calibration_frame->tx_profile,
        calibration_frame->rx_gain_index, calibration_frame->repeat_index,
        calibration_frame->sequence);

    if (ESP_OK == result &&
        !ofdm_calibration_encode(calibration_frame, calibration_payload)) {
        result = ESP_ERR_INVALID_ARG;
    }
    if (ESP_OK == result &&
        OFDM_FRAME_OK != ofdm_frame_build_calibration(
                             calibration_payload,
                             sizeof(calibration_payload), session_id,
                             &frame)) {
        result = ESP_FAIL;
    }
    if (ESP_OK == result &&
        OFDM_FRAME_OK != ofdm_frame_header_serialize(
                             &frame.header, header_wire)) {
        result = ESP_FAIL;
    }
    if (ESP_OK == result &&
        OFDM_FEC_OK != ofdm_fec_encode(
                           frame.payload, encoded, session_id, 0U)) {
        result = ESP_FAIL;
    }
    if (ESP_OK == result &&
        OFDM_PHY_OK != ofdm_phy_modulate_frame(
                           header_wire, encoded, s_frame_float)) {
        result = ESP_FAIL;
    }
    if (ESP_OK == result) {
        for (size_t index = 0U; index < OFDM_FRAME_SAMPLE_COUNT; ++index) {
            s_transmit_pcm[index] = float_to_pcm(s_frame_float[index],
                                                 &clip_count);
        }
        publish_calibration_progress(OFDM_LINK_STATE_TX_DATA,
                                     calibration_frame->sequence,
                                     "正在发送校准声波");
        result = ofdm_audio_write_mono(s_transmit_pcm,
                                       OFDM_FRAME_SAMPLE_COUNT);
    }
    if (ESP_OK == result) {
        result = ofdm_audio_finish_tx();
    }

    ESP_LOGI(TAG,
             "OFDM_CAL role=TX stage=%s run=%u seq=%u tx=%u rx=%u repeat=%u attempt=%u/%u waveform_tx=%u volume=%u pcm=%u clip=%lu result=%s",
             calibration_kind_name(calibration_frame->kind),
             (unsigned int)calibration_frame->run_id,
             (unsigned int)calibration_frame->sequence,
             (unsigned int)calibration_frame->tx_profile,
             (unsigned int)calibration_frame->rx_gain_index,
             (unsigned int)calibration_frame->repeat_index,
             (unsigned int)physical_attempt, (unsigned int)physical_total,
             (unsigned int)waveform_profile,
             (unsigned int)calibration_frame->volume_percent,
             (unsigned int)calibration_frame->pcm_scale,
             (unsigned long)clip_count, ESP_OK == result ? "OK" : "FAIL");
    return result;
}

static esp_err_t transmit_calibration_sequence(void)
{
    if (s_calibration_rx_armed || s_calibration_rx_active ||
        s_calibration_tx_active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_calibration_cancel_requested) {
        s_calibration_cancel_requested = false;
        return ESP_ERR_INVALID_STATE;
    }

    s_calibration_tx_active = true;
    s_calibration_command_pending = false;
    s_calibration_tail_guard = false;
    s_calibration_tail_guard_until_us = 0;
    s_calibration_deadline_us = esp_timer_get_time() +
                                OFDM_LINK_CAL_TIMEOUT_US;
    s_calibration_run_id = create_session_id();
    s_discard_receive = true;
    reset_calibration_capture();
    publish_calibration_progress(OFDM_LINK_STATE_TX_DATA, 0U,
                                 "准备发送校准序列");

    esp_err_t result = ofdm_audio_set_amp(true);
    if (ESP_OK == result) {
        vTaskDelay(pdMS_TO_TICKS(OFDM_LINK_TX_AMP_SETTLE_MS));
    }

    const bool amplifier_enabled = ESP_OK == result;
    bool user_cancelled = false;
    uint16_t physical_frame_count = 0U;
    for (uint16_t sequence = 0U;
         ESP_OK == result && OFDM_LINK_CAL_TOTAL_FRAMES > sequence;
        ++sequence) {
        if (calibration_should_abort()) {
            user_cancelled = s_calibration_cancel_requested;
            result = user_cancelled ? ESP_ERR_INVALID_STATE
                                    : ESP_ERR_TIMEOUT;
            break;
        }
        ofdm_calibration_frame_t calibration_frame = {0};
        if (!ofdm_calibration_prepare_frame(s_calibration_run_id, sequence,
                                            &calibration_frame)) {
            result = ESP_FAIL;
            break;
        }
        const uint8_t waveform_profile =
            OFDM_CAL_FRAME_SAMPLE == calibration_frame.kind
                ? calibration_frame.tx_profile
                : OFDM_LINK_CAL_SAFE_TX_PROFILE;
        const uint8_t physical_total =
            ofdm_calibration_transmit_count(sequence);

        for (uint8_t physical_attempt = 1U;
             ESP_OK == result && physical_total >= physical_attempt;
             ++physical_attempt) {
            if (calibration_should_abort()) {
                user_cancelled = s_calibration_cancel_requested;
                result = user_cancelled ? ESP_ERR_INVALID_STATE
                                        : ESP_ERR_TIMEOUT;
                break;
            }
            result = transmit_calibration_frame(
                &calibration_frame, waveform_profile, physical_attempt,
                physical_total);
            if (ESP_OK == result) {
                ++physical_frame_count;
            }
            if (ESP_OK == result && physical_total > physical_attempt) {
                vTaskDelay(pdMS_TO_TICKS(OFDM_LINK_CAL_FRAME_SETTLE_MS));
            }
        }
        if (ESP_OK != result) {
            break;
        }
        if (OFDM_CAL_FRAME_CONTROL == calibration_frame.kind) {
            vTaskDelay(pdMS_TO_TICKS(OFDM_LINK_CAL_GAIN_SETTLE_MS));
        }
        if (OFDM_CAL_FRAME_END != calibration_frame.kind) {
            vTaskDelay(pdMS_TO_TICKS(OFDM_LINK_CAL_FRAME_SETTLE_MS));
        }
    }

    if (amplifier_enabled) {
        const esp_err_t finish_result = ofdm_audio_finish_tx();
        if (ESP_OK == result && ESP_OK != finish_result) {
            result = finish_result;
        }
        const esp_err_t amp_result = ofdm_audio_set_amp(false);
        if (ESP_OK == result && ESP_OK != amp_result) {
            result = amp_result;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(OFDM_LINK_TX_GUARD_MS));
    const esp_err_t restore_result = restore_default_audio_profile();
    if (ESP_OK == result && ESP_OK != restore_result) {
        result = restore_result;
    }
    const uint16_t completed_run_id = s_calibration_run_id;
    s_calibration_tx_active = false;
    s_calibration_deadline_us = 0;
    s_calibration_cancel_requested = false;
    s_discard_receive = false;
    reset_calibration_capture();

    if (ESP_OK == result) {
        ESP_LOGI(TAG,
                 "OFDM_CAL role=TX stage=RESULT run=%u result=OK frames=%u physical_frames=%u end_repeats=%u",
                 (unsigned int)completed_run_id,
                 (unsigned int)OFDM_LINK_CAL_TOTAL_FRAMES,
                 (unsigned int)physical_frame_count,
                 (unsigned int)OFDM_CAL_END_TRANSMIT_COUNT);
        publish_snapshot(OFDM_LINK_STATE_TX_DONE, completed_run_id, 0U,
                         0U, 0U, 0U, true,
                         "校准序列已发出，等待接收端结果", "");
        s_return_idle_at_us = esp_timer_get_time() +
                              OFDM_LINK_CAL_RESULT_HOLD_US;
    } else {
        const char *reason = user_cancelled ? "USER" :
                             ESP_ERR_TIMEOUT == result ? "TIMEOUT" :
                             esp_err_to_name(result);
        ESP_LOGW(TAG,
                 "OFDM_CAL role=TX stage=%s run=%u result=FAIL reason=%s",
                 user_cancelled ? "CANCEL" : "RESULT",
                 (unsigned int)completed_run_id, reason);
        publish_snapshot(OFDM_LINK_STATE_ERROR, completed_run_id, 0U,
                         0U, 0U, 0U, false,
                         "校准未完成，已恢复监听", "");
        s_return_idle_at_us = esp_timer_get_time() +
                              OFDM_LINK_CAL_RESULT_HOLD_US;
    }
    return result;
}

static bool get_calibration_descriptor(uint16_t sequence,
                                       ofdm_calibration_frame_t *frame)
{
    if (NULL == frame || 0U == s_calibration_run_id) {
        return false;
    }
    return ofdm_calibration_prepare_frame(s_calibration_run_id, sequence,
                                           frame);
}

static bool calibration_metadata_matches(
    const ofdm_calibration_frame_t *actual,
    const ofdm_calibration_frame_t *expected)
{
    if (NULL == actual || NULL == expected) {
        return false;
    }
    return actual->kind == expected->kind &&
           actual->tx_profile == expected->tx_profile &&
           actual->rx_gain_index == expected->rx_gain_index &&
           actual->repeat_index == expected->repeat_index &&
           actual->repeat_count == expected->repeat_count &&
           actual->volume_percent == expected->volume_percent &&
           actual->pcm_scale == expected->pcm_scale &&
           actual->sequence == expected->sequence &&
           actual->total_frames == expected->total_frames &&
           actual->first_bin == expected->first_bin &&
           actual->last_bin == expected->last_bin &&
           actual->run_id == expected->run_id;
}

static void account_for_missing_calibration_frames(uint16_t sequence)
{
    while (s_calibration_next_sequence < sequence) {
        ofdm_calibration_frame_t missing = {0};
        if (get_calibration_descriptor(s_calibration_next_sequence,
                                       &missing)) {
            if (OFDM_CAL_FRAME_SAMPLE == missing.kind) {
                record_calibration_sample(missing.sequence, false, false, 0U,
                                          0U, NULL, "MISSING");
            } else {
                ESP_LOGW(TAG,
                         "OFDM_CAL role=RX stage=%s seq=%u result=MISS",
                         calibration_kind_name(missing.kind),
                         (unsigned int)missing.sequence);
            }
        }
        if (OFDM_CAL_FRAME_CONTROL == missing.kind ||
            (OFDM_CAL_FRAME_SAMPLE == missing.kind &&
             1U == missing.repeat_index)) {
            s_calibration_control_ready = false;
        }
        ++s_calibration_next_sequence;
    }
}

static void record_calibration_sample(uint16_t sequence,
                                      bool phy_ok,
                                      bool crc_ok,
                                      uint32_t peak,
                                      uint32_t clip_samples,
                                      const ofdm_phy_frame_metrics_t *metrics,
                                      const char *reason)
{
    ofdm_calibration_frame_t descriptor = {0};
    if (!get_calibration_descriptor(sequence, &descriptor) ||
        OFDM_CAL_FRAME_SAMPLE != descriptor.kind) {
        return;
    }
    ofdm_calibration_record_sample(
        &s_calibration_stats, descriptor.tx_profile,
        descriptor.rx_gain_index, phy_ok, crc_ok, peak, clip_samples,
        metrics);
    log_calibration_sample(sequence, descriptor.tx_profile,
                           descriptor.rx_gain_index, phy_ok, crc_ok, peak,
                           clip_samples, metrics, reason);
}

static void advance_calibration_failure(uint16_t sequence,
                                        bool phy_ok,
                                        bool crc_ok,
                                        uint32_t peak,
                                        uint32_t clip_samples,
                                        const ofdm_phy_frame_metrics_t *metrics,
                                        const char *reason)
{
    if (!s_calibration_rx_active || sequence < s_calibration_next_sequence) {
        return;
    }
    account_for_missing_calibration_frames(sequence);

    ofdm_calibration_frame_t descriptor = {0};
    if (!get_calibration_descriptor(sequence, &descriptor)) {
        return;
    }
    if (OFDM_CAL_FRAME_SAMPLE == descriptor.kind) {
        record_calibration_sample(sequence, phy_ok, crc_ok, peak,
                                  clip_samples, metrics, reason);
    } else {
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=%s seq=%u phy=%u crc=%u reason=%s",
                 calibration_kind_name(descriptor.kind),
                 (unsigned int)sequence, phy_ok ? 1U : 0U,
                 crc_ok ? 1U : 0U, NULL != reason ? reason : "UNKNOWN");
    }
    if (OFDM_CAL_FRAME_CONTROL == descriptor.kind) {
        s_calibration_control_ready = false;
    }
    if (OFDM_CAL_FRAME_SAMPLE == descriptor.kind &&
        1U == descriptor.repeat_index) {
        s_calibration_control_ready = false;
    }
    s_calibration_next_sequence = (uint16_t)(sequence + 1U);
    refresh_calibration_deadline();
    if (ofdm_calibration_is_end_sequence(sequence)) {
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=END run=%u seq=%u result=RECOVERED reason=%s",
                 (unsigned int)s_calibration_run_id,
                 (unsigned int)sequence,
                 NULL != reason ? reason : "UNKNOWN");
        finish_rx_calibration(true, "END_RECOVERED");
        return;
    }
    publish_calibration_progress(OFDM_LINK_STATE_RX_DATA, sequence,
                                 "正在接收校准数据");
}

static bool set_calibration_gain_for_descriptor(
    const ofdm_calibration_frame_t *descriptor)
{
    if (NULL == descriptor) {
        return false;
    }
    if (ESP_OK != apply_rx_gain_index(descriptor->rx_gain_index)) {
        return false;
    }
    s_calibration_expected_tx = descriptor->tx_profile;
    s_calibration_expected_rx = descriptor->rx_gain_index;
    s_calibration_control_ready = true;
    return true;
}

static bool prepare_calibration_sample_gain(
    const ofdm_calibration_frame_t *descriptor,
    bool *gain_fallback)
{
    if (NULL == descriptor || NULL == gain_fallback ||
        OFDM_CAL_FRAME_SAMPLE != descriptor->kind) {
        return false;
    }
    *gain_fallback = false;
    if (s_calibration_control_ready &&
        s_calibration_expected_tx == descriptor->tx_profile &&
        s_calibration_expected_rx == descriptor->rx_gain_index) {
        return true;
    }
    if (!set_calibration_gain_for_descriptor(descriptor)) {
        return false;
    }
    *gain_fallback = true;
    return true;
}

static bool consume_header_only_calibration_frame(uint16_t sequence,
                                                  const char *reason)
{
    if (!s_calibration_rx_active || sequence != s_calibration_next_sequence) {
        return false;
    }
    ofdm_calibration_frame_t descriptor = {0};
    if (!get_calibration_descriptor(sequence, &descriptor) ||
        !ofdm_calibration_is_header_recoverable_sequence(sequence)) {
        return false;
    }
    s_calibration_deadline_us = esp_timer_get_time() +
                                OFDM_LINK_CAL_TIMEOUT_US;
    if (OFDM_CAL_FRAME_CONTROL == descriptor.kind) {
        if (!set_calibration_gain_for_descriptor(&descriptor)) {
            finish_rx_calibration(false, "AUDIO");
            return true;
        }
        ++s_calibration_next_sequence;
        refresh_calibration_deadline();
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=CONTROL run=%u seq=%u tx=%u rx=%u gain=%.1f result=HEADER_ONLY reason=%s",
                 (unsigned int)s_calibration_run_id,
                 (unsigned int)sequence,
                 (unsigned int)descriptor.tx_profile,
                 (unsigned int)descriptor.rx_gain_index,
                 (double)ofdm_audio_get_input_gain_db(),
                 NULL != reason ? reason : "UNKNOWN");
        publish_calibration_progress(OFDM_LINK_STATE_RX_DATA, sequence,
                                     "已从帧头切换增益，正在接收测量帧");
        return true;
    }
    ESP_LOGW(TAG,
             "OFDM_CAL role=RX stage=END run=%u seq=%u result=HEADER_ONLY reason=%s",
             (unsigned int)s_calibration_run_id, (unsigned int)sequence,
             NULL != reason ? reason : "UNKNOWN");
    finish_rx_calibration(true, "END_HEADER_ONLY");
    return true;
}

static bool begin_rx_calibration(void)
{
    if (s_calibration_tx_active || s_calibration_rx_active ||
        s_calibration_rx_armed) {
        return false;
    }
    if (s_calibration_cancel_requested) {
        s_calibration_cancel_requested = false;
        return false;
    }

    s_calibration_command_pending = false;
    s_calibration_tail_guard = false;
    s_calibration_tail_guard_until_us = 0;
    s_calibration_rx_armed = true;
    s_calibration_rx_active = false;
    s_calibration_deadline_us = esp_timer_get_time() +
                                OFDM_LINK_CAL_TIMEOUT_US;
    s_calibration_run_id = 0U;
    s_calibration_next_sequence = 0U;
    s_calibration_expected_tx = 0U;
    s_calibration_expected_rx = 0U;
    s_calibration_control_ready = false;
    ofdm_calibration_reset_stats(&s_calibration_stats);
    s_return_idle_at_us = 0;
    reset_calibration_capture();

    const esp_err_t result = restore_default_audio_profile();
    if (ESP_OK != result) {
        s_calibration_rx_armed = false;
        s_calibration_deadline_us = 0;
        publish_snapshot(OFDM_LINK_STATE_ERROR, 0U, 0U, 0U, 0U, 0U,
                         false, "校准音频配置失败", "");
        return false;
    }

    publish_snapshot(OFDM_LINK_STATE_RX_DATA, 0U, 0U,
                     (uint8_t)OFDM_LINK_CAL_TOTAL_FRAMES, 0U, 0U, false,
                     "校准接收待命，等待发送端", "");
    ESP_LOGI(TAG,
             "OFDM_CAL role=RX stage=READY total=%u timeout_ms=%u default_tx=%u default_rx=%u",
             (unsigned int)OFDM_LINK_CAL_TOTAL_FRAMES,
             (unsigned int)(OFDM_LINK_CAL_TIMEOUT_US / 1000),
             (unsigned int)OFDM_LINK_CAL_SAFE_TX_PROFILE,
             (unsigned int)OFDM_LINK_CAL_DEFAULT_RX_GAIN);
    return true;
}

static void handle_calibration_header_failure(
    uint16_t session_id,
    bool phy_ok,
    uint32_t peak,
    uint32_t clip_samples,
    const ofdm_phy_frame_metrics_t *metrics,
    const char *reason)
{
    uint16_t sequence = 0U;
    if (!s_calibration_rx_active ||
        !ofdm_calibration_find_sequence(session_id, &sequence)) {
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=FRAME result=REJECTED session=%u reason=%s",
                 (unsigned int)session_id,
                 NULL != reason ? reason : "UNKNOWN");
        return;
    }
    if (sequence < s_calibration_next_sequence) {
        ESP_LOGI(TAG,
                 "OFDM_CAL role=RX stage=FRAME session=%u seq=%u result=DUPLICATE",
                 (unsigned int)session_id, (unsigned int)sequence);
        return;
    }
    account_for_missing_calibration_frames(sequence);
    if (consume_header_only_calibration_frame(sequence, reason)) {
        return;
    }
    ofdm_calibration_frame_t descriptor = {0};
    if (get_calibration_descriptor(sequence, &descriptor) &&
        OFDM_CAL_FRAME_SAMPLE == descriptor.kind) {
        bool gain_fallback = false;
        if (!prepare_calibration_sample_gain(&descriptor, &gain_fallback)) {
            finish_rx_calibration(false, "AUDIO");
            return;
        }
        if (gain_fallback) {
            advance_calibration_failure(
                sequence, false, false, 0U, 0U, NULL,
                0U == descriptor.repeat_index ? "GAIN_FALLBACK"
                                               : "NO_CONTROL");
            return;
        }
    }
    advance_calibration_failure(sequence, phy_ok, false, peak, clip_samples,
                                metrics, reason);
}

static void consume_valid_calibration_frame(
    const ofdm_frame_t *frame,
    const ofdm_calibration_frame_t *calibration_frame,
    const ofdm_phy_frame_metrics_t *metrics,
    uint32_t peak,
    uint32_t clip_samples)
{
    if (NULL == frame || NULL == calibration_frame) {
        return;
    }

    uint16_t sequence = calibration_frame->sequence;
    if (!s_calibration_rx_armed) {
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=%s seq=%u result=REJECTED reason=NOT_ARMED",
                 calibration_kind_name(calibration_frame->kind),
                 (unsigned int)sequence);
        return;
    }

    if (OFDM_CAL_FRAME_START == calibration_frame->kind) {
        if (0U != sequence) {
            return;
        }
        ofdm_calibration_frame_t expected_start = {0};
        if (!ofdm_calibration_prepare_frame(calibration_frame->run_id, 0U,
                                            &expected_start) ||
            !calibration_metadata_matches(calibration_frame,
                                          &expected_start)) {
            ESP_LOGW(TAG,
                     "OFDM_CAL role=RX stage=START seq=%u result=REJECTED reason=METADATA",
                     (unsigned int)sequence);
            return;
        }
        ofdm_calibration_reset_stats(&s_calibration_stats);
        s_calibration_run_id = calibration_frame->run_id;
        s_calibration_rx_active = true;
        s_calibration_next_sequence = 1U;
        s_calibration_expected_tx = 0U;
        s_calibration_expected_rx = 0U;
        s_calibration_control_ready = false;
        s_calibration_deadline_us = esp_timer_get_time() +
                                    OFDM_LINK_CAL_TIMEOUT_US;
        if (ESP_OK !=
            apply_rx_gain_index(OFDM_LINK_CAL_DEFAULT_RX_GAIN)) {
            finish_rx_calibration(false, "AUDIO");
            return;
        }
        ESP_LOGI(TAG,
                 "OFDM_CAL role=RX stage=START run=%u seq=%u tx=%u rx=%u result=OK",
                 (unsigned int)s_calibration_run_id, (unsigned int)sequence,
                 (unsigned int)calibration_frame->tx_profile,
                 (unsigned int)calibration_frame->rx_gain_index);
        publish_calibration_progress(OFDM_LINK_STATE_RX_DATA, sequence,
                                     "校准已开始，正在接收控制帧");
        return;
    }

    if (!s_calibration_rx_active &&
        ofdm_calibration_can_recover_start(calibration_frame)) {
        ofdm_calibration_reset_stats(&s_calibration_stats);
        s_calibration_run_id = calibration_frame->run_id;
        s_calibration_rx_active = true;
        s_calibration_next_sequence = 1U;
        s_calibration_expected_tx = 0U;
        s_calibration_expected_rx = 0U;
        s_calibration_control_ready = false;
        s_calibration_deadline_us = esp_timer_get_time() +
                                    OFDM_LINK_CAL_TIMEOUT_US;
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=START run=%u seq=0 result=RECOVERED first_seq=%u",
                 (unsigned int)s_calibration_run_id,
                 (unsigned int)sequence);
    }
    if (!s_calibration_rx_active ||
        calibration_frame->run_id != s_calibration_run_id) {
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=%s seq=%u result=REJECTED reason=RUN",
                 calibration_kind_name(calibration_frame->kind),
                 (unsigned int)sequence);
        return;
    }
    if (sequence < s_calibration_next_sequence) {
        ESP_LOGI(TAG,
                 "OFDM_CAL role=RX stage=%s seq=%u result=DUPLICATE",
                 calibration_kind_name(calibration_frame->kind),
                 (unsigned int)sequence);
        return;
    }
    account_for_missing_calibration_frames(sequence);

    ofdm_calibration_frame_t expected = {0};
    if (!get_calibration_descriptor(sequence, &expected) ||
        !calibration_metadata_matches(calibration_frame, &expected)) {
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=%s seq=%u result=REJECTED reason=SEQUENCE",
                 calibration_kind_name(calibration_frame->kind),
                 (unsigned int)sequence);
        return;
    }
    s_calibration_deadline_us = esp_timer_get_time() +
                                OFDM_LINK_CAL_TIMEOUT_US;

    if (OFDM_CAL_FRAME_CONTROL == calibration_frame->kind) {
        if (!set_calibration_gain_for_descriptor(calibration_frame)) {
            finish_rx_calibration(false, "AUDIO");
            return;
        }
        ++s_calibration_next_sequence;
        refresh_calibration_deadline();
        ESP_LOGI(TAG,
                 "OFDM_CAL role=RX stage=CONTROL run=%u seq=%u tx=%u rx=%u gain=%.1f result=OK",
                 (unsigned int)s_calibration_run_id,
                 (unsigned int)sequence,
                 (unsigned int)calibration_frame->tx_profile,
                 (unsigned int)calibration_frame->rx_gain_index,
                 (double)ofdm_audio_get_input_gain_db());
        publish_calibration_progress(OFDM_LINK_STATE_RX_DATA, sequence,
                                     "已切换增益，正在接收测量帧");
        return;
    }

    if (OFDM_CAL_FRAME_SAMPLE == calibration_frame->kind) {
        bool gain_fallback = false;
        if (!prepare_calibration_sample_gain(calibration_frame,
                                             &gain_fallback)) {
            finish_rx_calibration(false, "AUDIO");
            return;
        }
        if (gain_fallback) {
            record_calibration_sample(
                sequence, false, false, 0U, 0U, NULL,
                0U == calibration_frame->repeat_index ? "GAIN_FALLBACK"
                                                       : "NO_CONTROL");
        } else {
            record_calibration_sample(sequence, true, true, peak,
                                      clip_samples, metrics, "OK");
        }
        if (1U == calibration_frame->repeat_index) {
            s_calibration_control_ready = false;
        }
        ++s_calibration_next_sequence;
        refresh_calibration_deadline();
        publish_calibration_progress(OFDM_LINK_STATE_RX_DATA, sequence,
                                     "正在记录测量结果");
        return;
    }

    if (OFDM_CAL_FRAME_END == calibration_frame->kind) {
        ESP_LOGI(TAG,
                 "OFDM_CAL role=RX stage=END run=%u seq=%u result=OK",
                 (unsigned int)s_calibration_run_id, (unsigned int)sequence);
        finish_rx_calibration(true, "OK");
    }
}

static esp_err_t transmit_message(void)
{
    const size_t message_length = sizeof(s_test_message) - 1U;
    const uint8_t frame_count = (uint8_t)(
        (message_length + OFDM_FRAME_PAYLOAD_BYTES - 1U) /
        OFDM_FRAME_PAYLOAD_BYTES);
    const uint16_t session_id = create_session_id();
    uint32_t clip_count = 0U;
    esp_err_t result = ESP_OK;

    if (OFDM_MESSAGE_OK !=
        ofdm_message_validate(s_test_message, message_length)) {
        publish_snapshot(OFDM_LINK_STATE_ERROR, session_id, 0U, frame_count,
                         0U, (uint16_t)message_length, false,
                         "内置文本无效", "");
        return ESP_ERR_INVALID_ARG;
    }

    s_discard_receive = true;
    clear_audio_queue();
    reset_capture();
    publish_snapshot(OFDM_LINK_STATE_TX_DATA, session_id, 0U, frame_count,
                     0U, (uint16_t)message_length, false,
                     "正在发送声波", "");
    result = ofdm_audio_set_amp(true);
    if (ESP_OK == result) {
        vTaskDelay(pdMS_TO_TICKS(OFDM_LINK_TX_AMP_SETTLE_MS));
        ESP_LOGI(TAG, "OFDM_TX stage=amp_settle ms=%u result=OK",
                 (unsigned int)OFDM_LINK_TX_AMP_SETTLE_MS);
    }
    for (uint8_t frame_index = 0U;
         ESP_OK == result && frame_index < frame_count; ++frame_index) {
        ofdm_frame_t frame = {0};
        uint8_t header_wire[OFDM_FRAME_HEADER_BYTES] = {0};
        uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};

        if (OFDM_FRAME_OK != ofdm_frame_build(
                                 s_test_message, message_length, session_id,
                                 frame_index, &frame) ||
            OFDM_FRAME_OK != ofdm_frame_header_serialize(
                                 &frame.header, header_wire) ||
            OFDM_FEC_OK != ofdm_fec_encode(
                               frame.payload, encoded, session_id,
                               frame_index) ||
            OFDM_PHY_OK != ofdm_phy_modulate_frame(
                               header_wire, encoded, s_frame_float)) {
            result = ESP_FAIL;
            break;
        }

        for (size_t index = 0U; index < OFDM_FRAME_SAMPLE_COUNT; ++index) {
            s_transmit_pcm[index] = float_to_pcm(s_frame_float[index],
                                                &clip_count);
        }
        publish_snapshot(OFDM_LINK_STATE_TX_DATA, session_id,
                         (uint8_t)(frame_index + 1U), frame_count, 0U,
                         (uint16_t)message_length, false,
                         "正在发送声波", NULL);
        result = ofdm_audio_write_mono(s_transmit_pcm,
                                       OFDM_FRAME_SAMPLE_COUNT);
        ESP_LOGI(TAG,
                 "OFDM_TX session=%u frame=%u/%u payload=%u pcm=%u result=%s",
                 (unsigned int)session_id, (unsigned int)(frame_index + 1U),
                 (unsigned int)frame_count,
                 (unsigned int)frame.header.payload_len,
                 (unsigned int)OFDM_FRAME_SAMPLE_COUNT,
                 ESP_OK == result ? "OK" : "FAIL");
    }
    if (ESP_OK == result) {
        result = ofdm_audio_finish_tx();
    }
    esp_err_t amp_result = ofdm_audio_set_amp(false);
    if (ESP_OK == result && ESP_OK != amp_result) {
        result = amp_result;
    }
    vTaskDelay(pdMS_TO_TICKS(OFDM_LINK_TX_GUARD_MS));
    clear_audio_queue();
    reset_capture();
    s_discard_receive = false;

    if (ESP_OK == result) {
        publish_snapshot(OFDM_LINK_STATE_TX_DONE, session_id, frame_count,
                         frame_count, 0U, (uint16_t)message_length, true,
                         "声波已发出", NULL);
        ESP_LOGI(TAG,
                 "OFDM_TX session=%u complete frames=%u bytes=%u audio_sent=true clip=%lu",
                 (unsigned int)session_id, (unsigned int)frame_count,
                 (unsigned int)message_length, (unsigned long)clip_count);
    } else {
        increment_counter(&s_health.tx_underrun);
        publish_snapshot(OFDM_LINK_STATE_ERROR, session_id, 0U, frame_count,
                         0U, (uint16_t)message_length, false,
                         "发送失败，已恢复监听", NULL);
        ESP_LOGE(TAG, "OFDM_DROP stage=TX reason=AUDIO error=%s",
                 esp_err_to_name(result));
    }
    schedule_idle_return();
    return result;
}

static void consume_capture(size_t samples)
{
    if (samples >= s_capture_count) {
        reset_capture();
        return;
    }
    const size_t remaining = s_capture_count - samples;
    memmove(s_capture_pcm, &s_capture_pcm[samples],
            remaining * sizeof(s_capture_pcm[0]));
    memmove(s_capture_float, &s_capture_float[samples],
            remaining * sizeof(s_capture_float[0]));
    s_capture_count = remaining;
    if (samples <= s_next_search_offset) {
        s_next_search_offset -= samples;
    } else {
        s_next_search_offset = 0U;
    }
}

static bool decode_candidate(size_t frame_offset,
                             float chirp_score,
                             int32_t timing_correction_samples,
                             bool used_training_match,
                             size_t *consumed_samples)
{
    uint8_t header_wire[OFDM_FRAME_HEADER_BYTES] = {0};
    uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t decoded[OFDM_FEC_DATA_BYTES] = {0};
    uint8_t complete_message[OFDM_MESSAGE_MAX_BYTES + 1U] = {0};
    ofdm_phy_frame_metrics_t metrics = {0};
    ofdm_frame_t frame = {0};
    size_t complete_length = 0U;
    uint16_t corrected_symbols = 0U;

    *consumed_samples = frame_offset + OFDM_SYNC_COARSE_STEP;
    for (size_t index = 0U; index < OFDM_FRAME_SAMPLE_COUNT; ++index) {
        s_frame_float[index] =
            (float)s_capture_pcm[frame_offset + index] / 32768.0F;
    }
    const int64_t started_us = esp_timer_get_time();
    if (OFDM_PHY_OK != ofdm_phy_demodulate_frame(
                           s_frame_float, header_wire, encoded, &metrics)) {
        increment_counter(&s_health.sync_fail);
        ESP_LOGW(TAG,
                 "OFDM_DROP stage=SYNC reason=TRAINING chirp=%.3f mode=%s timing=%ld sc=%.3f lts=%.3f",
                 chirp_score,
                 used_training_match ? "COMPOSITE" : "CHIRP",
                 (long)timing_correction_samples,
                 metrics.sc_score, metrics.lts_score);
        return false;
    }
    if (OFDM_FRAME_OK != ofdm_frame_header_parse(header_wire,
                                                 &frame.header)) {
        increment_counter(&s_health.crc_fail);
        ESP_LOGW(TAG,
                 "OFDM_DROP stage=HEADER reason=CRC chirp=%.3f sc=%.3f lts=%.3f header_evm_db=%.1f payload_evm_db=%.1f",
                 chirp_score, metrics.sc_score, metrics.lts_score,
                 metrics.header_evm_db, metrics.payload_evm_db);
        return false;
    }

    if (ofdm_frame_is_calibration(&frame.header)) {
        uint32_t peak = 0U;
        uint32_t clip_samples = 0U;
        measure_pcm_frame(&s_capture_pcm[frame_offset],
                          OFDM_FRAME_SAMPLE_COUNT, &peak, &clip_samples);
        *consumed_samples = frame_offset + OFDM_FRAME_SAMPLE_COUNT;
        if (!s_calibration_rx_armed) {
            ESP_LOGW(TAG,
                     "OFDM_CAL role=RX stage=FRAME session=%u result=REJECTED reason=NOT_ARMED",
                     (unsigned int)frame.header.session_id);
            restore_receive_state_after_candidate_failure();
            return true;
        }

        const ofdm_fec_result_t fec_result = ofdm_fec_decode(
            encoded, decoded, frame.header.session_id,
            frame.header.frame_index, &corrected_symbols);
        if (OFDM_FEC_OK != fec_result) {
            increment_counter(&s_health.crc_fail);
            handle_calibration_header_failure(
                frame.header.session_id, true, peak, clip_samples, &metrics,
                "FEC");
            return true;
        }
        memcpy(frame.payload, decoded, sizeof(frame.payload));
        if (OFDM_FRAME_OK != ofdm_frame_validate(&frame)) {
            increment_counter(&s_health.crc_fail);
            handle_calibration_header_failure(
                frame.header.session_id, true, peak, clip_samples, &metrics,
                "CRC");
            return true;
        }
        ofdm_calibration_frame_t calibration_frame = {0};
        if (!ofdm_calibration_decode(frame.payload, &calibration_frame) ||
            !ofdm_calibration_session_matches(frame.header.session_id,
                                              &calibration_frame)) {
            increment_counter(&s_health.crc_fail);
            handle_calibration_header_failure(
                frame.header.session_id, true, peak, clip_samples, &metrics,
                "METADATA");
            return true;
        }

        increment_counter(&s_health.sync_ok);
        add_counter(&s_health.rs_fixed, corrected_symbols);
        s_last_valid_frame_us = esp_timer_get_time();
        update_dsp_max((uint32_t)(s_last_valid_frame_us - started_us));
        ESP_LOGI(TAG,
                 "OFDM_SYNC score=%.3f sample=%u lts=%.3f cfo=not_measured calibration=1",
                 chirp_score, (unsigned int)frame_offset, metrics.lts_score);
        consume_valid_calibration_frame(&frame, &calibration_frame,
                                        &metrics, peak, clip_samples);
        return true;
    }

    if (s_calibration_rx_armed || s_calibration_rx_active) {
        *consumed_samples = frame_offset + OFDM_FRAME_SAMPLE_COUNT;
        ESP_LOGW(TAG,
                 "OFDM_CAL role=RX stage=FRAME session=%u result=REJECTED reason=ORDINARY_FRAME",
                 (unsigned int)frame.header.session_id);
        publish_calibration_progress(OFDM_LINK_STATE_RX_DATA,
                                     s_calibration_next_sequence,
                                     s_calibration_rx_active
                                         ? "校准接收中，忽略普通数据帧"
                                         : "校准接收待命，等待发送端");
        return true;
    }

    const bool continuing_session =
        s_reassembly.active &&
        s_reassembly.session_id == frame.header.session_id;
    const uint8_t received_count =
        continuing_session
            ? ofdm_reassembly_received_count(&s_reassembly)
            : 0U;
    publish_snapshot(OFDM_LINK_STATE_RX_DATA, frame.header.session_id,
                     received_count, frame.header.frame_count,
                     continuing_session ? s_reassembly.received_bitmap : 0U,
                     frame.header.message_len, false, "正在接收数据", "");
    if (OFDM_FEC_OK != ofdm_fec_decode(
                           encoded, decoded, frame.header.session_id,
                           frame.header.frame_index, &corrected_symbols)) {
        increment_counter(&s_health.crc_fail);
        ESP_LOGW(TAG,
                 "OFDM_DROP stage=PAYLOAD reason=FEC session=%u frame=%u sc=%.3f lts=%.3f header_evm_db=%.1f payload_evm_db=%.1f",
                 (unsigned int)frame.header.session_id,
                 (unsigned int)frame.header.frame_index,
                 metrics.sc_score, metrics.lts_score,
                 metrics.header_evm_db, metrics.payload_evm_db);
        return false;
    }
    memcpy(frame.payload, decoded, sizeof(frame.payload));
    const ofdm_reassembly_result_t reassembly_result =
        ofdm_reassembly_accept(&s_reassembly, &frame, complete_message,
                               sizeof(complete_message), &complete_length);
    if (OFDM_REASSEMBLY_REJECTED == reassembly_result ||
        OFDM_REASSEMBLY_INVALID_ARGUMENT == reassembly_result) {
        increment_counter(&s_health.crc_fail);
        ESP_LOGW(TAG,
                 "OFDM_DROP stage=PAYLOAD reason=CRC session=%u frame=%u sc=%.3f lts=%.3f header_evm_db=%.1f payload_evm_db=%.1f",
                 (unsigned int)frame.header.session_id,
                 (unsigned int)frame.header.frame_index,
                 metrics.sc_score, metrics.lts_score,
                 metrics.header_evm_db, metrics.payload_evm_db);
        return false;
    }

    increment_counter(&s_health.sync_ok);
    add_counter(&s_health.rs_fixed, corrected_symbols);
    s_last_valid_frame_us = esp_timer_get_time();
    const uint32_t elapsed_us = (uint32_t)(s_last_valid_frame_us - started_us);
    update_dsp_max(elapsed_us);
    *consumed_samples = frame_offset + OFDM_FRAME_SAMPLE_COUNT;
    ESP_LOGI(TAG,
             "OFDM_SYNC score=%.3f sample=%u lts=%.3f cfo=not_measured",
             chirp_score, (unsigned int)frame_offset, metrics.lts_score);
    ESP_LOGI(TAG,
             "OFDM_RX session=%u frame=%u/%u payload=%u rs_fixed=%u evm_db=%.1f crc=OK",
             (unsigned int)frame.header.session_id,
             (unsigned int)(frame.header.frame_index + 1U),
             (unsigned int)frame.header.frame_count,
             (unsigned int)frame.header.payload_len,
             (unsigned int)corrected_symbols, metrics.payload_evm_db);

    if (OFDM_REASSEMBLY_COMPLETE == reassembly_result) {
        publish_snapshot(OFDM_LINK_STATE_RX_OK, frame.header.session_id,
                         frame.header.frame_count, frame.header.frame_count,
                         0U, (uint16_t)complete_length, false,
                         "完整文字接收成功", (const char *)complete_message);
        ESP_LOGI(TAG, "OFDM_RX session=%u complete bytes=%u crc=OK utf8=OK",
                 (unsigned int)frame.header.session_id,
                 (unsigned int)complete_length);
        schedule_idle_return();
    } else {
        const uint8_t accepted_count =
            ofdm_reassembly_received_count(&s_reassembly);
        publish_snapshot(OFDM_LINK_STATE_RX_DATA, frame.header.session_id,
                         accepted_count,
                         frame.header.frame_count,
                         s_reassembly.received_bitmap,
                         frame.header.message_len, false,
                         "正在接收数据", NULL);
    }
    return true;
}

static void process_capture(void)
{
    while (OFDM_FRAME_SAMPLE_COUNT + OFDM_SYNC_TIMING_SEARCH_SAMPLES <=
           s_capture_count) {
        const size_t last_search_offset =
            s_capture_count - OFDM_FRAME_SAMPLE_COUNT -
            OFDM_SYNC_TIMING_SEARCH_SAMPLES;
        const size_t first_search_offset =
            OFDM_SYNC_COARSE_STEP < s_next_search_offset
                ? s_next_search_offset - OFDM_SYNC_COARSE_STEP
                : 0U;
        if (last_search_offset < first_search_offset) {
            return;
        }
        const size_t context_offset =
            OFDM_SYNC_TIMING_SEARCH_SAMPLES < first_search_offset
                ? first_search_offset - OFDM_SYNC_TIMING_SEARCH_SAMPLES
                : 0U;
        const size_t local_first_search_offset =
            first_search_offset - context_offset;
        ofdm_sync_match_t match = {0};
        const ofdm_sync_result_t sync_result = ofdm_sync_find_frame_from(
            &s_capture_float[context_offset],
            s_capture_count - context_offset, local_first_search_offset,
            &match);
        update_chirp_activity(match.best_chirp_score,
                              OFDM_SYNC_OK == sync_result);
        if (OFDM_SYNC_OK != sync_result) {
            if (OFDM_SYNC_COMPOSITE_MIN_SCORE <=
                match.best_chirp_score) {
                ESP_LOGI(TAG,
                         "OFDM_SYNC candidate=REJECTED chirp=%.3f timing=%ld sc=%.3f lts=%.3f sc_period=%u/%.3f lts_period=%u/%.3f lts_long=%u/%.3f rms=%.4f/%.4f",
                         match.best_chirp_score,
                         (long)match.timing_correction_samples,
                         match.training_sc_score,
                         match.training_lts_score,
                         (unsigned int)match.sc_period_lag,
                         match.sc_period_score,
                         (unsigned int)match.lts_short_period_lag,
                         match.lts_short_period_score,
                         (unsigned int)match.lts_long_period_lag,
                         match.lts_long_period_score,
                         match.sc_rms, match.lts_rms);
            }
            const size_t local_last_frame_offset =
                last_search_offset - first_search_offset;
            const size_t last_coarse_offset =
                local_last_frame_offset -
                (local_last_frame_offset % OFDM_SYNC_COARSE_STEP);
            s_next_search_offset = first_search_offset +
                                   last_coarse_offset +
                                   OFDM_SYNC_COARSE_STEP;
            if (OFDM_LINK_CAPTURE_SAMPLES <= s_capture_count) {
                consume_capture(OFDM_LINK_SEARCH_SAMPLES);
            }
            return;
        }
        match.frame_offset += context_offset;
        match.chirp_offset += context_offset;
        const size_t chirp_frame_offset =
            match.chirp_offset - OFDM_FRAME_CHIRP_OFFSET;
        s_next_search_offset = chirp_frame_offset + OFDM_SYNC_COARSE_STEP;

        publish_snapshot(OFDM_LINK_STATE_RX_SYNC, 0U, 0U, 0U, 0U, 0U,
                         false, "检测到声学帧", NULL);
        size_t consumed_samples = 0U;
        const bool decoded = decode_candidate(
            match.frame_offset, match.chirp_score,
            match.timing_correction_samples,
            match.used_training_match, &consumed_samples);
        if (!decoded) {
            consumed_samples = chirp_frame_offset + OFDM_SYNC_COARSE_STEP;
            restore_receive_state_after_candidate_failure();
        }
        consume_capture(consumed_samples);
    }
}

static void append_audio_block(const ofdm_audio_block_t *block)
{
    if (NULL == block) {
        return;
    }
    if (OFDM_LINK_CAPTURE_SAMPLES <
        s_capture_count + OFDM_AUDIO_READ_FRAMES) {
        const size_t overflow = s_capture_count + OFDM_AUDIO_READ_FRAMES -
                                OFDM_LINK_CAPTURE_SAMPLES;
        consume_capture(overflow);
    }
    memcpy(&s_capture_pcm[s_capture_count], block->samples,
           sizeof(block->samples));
    for (size_t index = 0U; index < OFDM_AUDIO_READ_FRAMES; ++index) {
        s_capture_float[s_capture_count + index] =
            (float)block->samples[index] / 32768.0F;
    }
    s_capture_count += OFDM_AUDIO_READ_FRAMES;
}

static void audio_task(void *argument)
{
    (void)argument;
    ofdm_audio_block_t block = {0};
    while (true) {
        esp_err_t result = ofdm_audio_read_mic1(
            block.samples, OFDM_AUDIO_READ_FRAMES,
            OFDM_LINK_AUDIO_TIMEOUT_MS);
        if (ESP_OK != result) {
            if (ESP_ERR_TIMEOUT != result) {
                increment_counter(&s_health.rx_drop);
            }
            continue;
        }
        if (s_discard_receive || s_calibration_tail_guard) {
            continue;
        }
        update_rx_activity(block.samples, OFDM_AUDIO_READ_FRAMES);
        const BaseType_t queued = xQueueSend(s_audio_queue, &block, 0U);
        update_rx_queue_peak(
            (uint32_t)uxQueueMessagesWaiting(s_audio_queue));
        if (pdTRUE != queued) {
            increment_counter(&s_health.rx_drop);
        }
    }
}

static void process_link_command(ofdm_link_command_t command)
{
    switch (command) {
        case OFDM_LINK_COMMAND_SEND:
            if (s_calibration_rx_armed || s_calibration_rx_active ||
                s_calibration_tx_active) {
                ESP_LOGW(TAG,
                         "OFDM_TX action=command result=REJECTED reason=CALIBRATION");
            } else {
                (void)transmit_message();
            }
            break;
        case OFDM_LINK_COMMAND_RX_CALIBRATION:
            s_calibration_command_pending = false;
            if (!begin_rx_calibration()) {
                ESP_LOGW(TAG,
                         "OFDM_CAL role=RX stage=READY result=REJECTED");
            }
            break;
        case OFDM_LINK_COMMAND_TX_CALIBRATION:
            s_calibration_command_pending = false;
            if (ESP_OK != transmit_calibration_sequence()) {
                ESP_LOGW(TAG,
                         "OFDM_CAL role=TX stage=RESULT result=REJECTED");
            }
            break;
        case OFDM_LINK_COMMAND_STOP_CALIBRATION:
            s_calibration_cancel_requested = true;
            break;
        default:
            break;
    }
}

static void modem_task(void *argument)
{
    (void)argument;
    ofdm_audio_block_t block = {0};
    ofdm_link_command_t command = 0;

    publish_snapshot(OFDM_LINK_STATE_IDLE_RX, 0U, 0U, 0U, 0U,
                     0U, false, "监听声学链路", "");
    while (true) {
        if (pdTRUE == xQueueReceive(s_command_queue, &command, 0U)) {
            process_link_command(command);
        }

        if (s_calibration_cancel_requested && !s_calibration_tx_active &&
            (s_calibration_rx_armed || s_calibration_rx_active)) {
            finish_rx_calibration(false, "USER");
        }

        if (pdTRUE == xQueueReceive(s_audio_queue, &block,
                                    pdMS_TO_TICKS(OFDM_LINK_MODEM_WAIT_MS))) {
            const int64_t started_us = esp_timer_get_time();
            append_audio_block(&block);
            process_capture();
            update_dsp_max((uint32_t)(esp_timer_get_time() - started_us));
        }
        const int64_t now_us = esp_timer_get_time();
        if (s_calibration_tail_guard &&
            now_us >= s_calibration_tail_guard_until_us) {
            s_calibration_tail_guard = false;
            s_calibration_tail_guard_until_us = 0;
        }
        if (s_calibration_rx_armed && 0 != s_calibration_deadline_us &&
            now_us >= s_calibration_deadline_us) {
            s_calibration_cancel_requested = false;
            if (s_calibration_rx_active &&
                OFDM_LINK_CAL_TOTAL_FRAMES - 1U ==
                    s_calibration_next_sequence) {
                ESP_LOGW(TAG,
                         "OFDM_CAL role=RX stage=END result=RECOVERED reason=TIMEOUT");
                finish_rx_calibration(true, "END_TIMEOUT_RECOVERED");
            } else {
                finish_rx_calibration(false, "TIMEOUT");
            }
        } else if (s_reassembly.active &&
                   OFDM_LINK_SESSION_TIMEOUT_US <
                       now_us - s_last_valid_frame_us) {
            ESP_LOGW(TAG, "OFDM_DROP stage=SESSION reason=TIMEOUT session=%u",
                     (unsigned int)s_reassembly.session_id);
            ofdm_reassembly_reset(&s_reassembly);
            publish_snapshot(OFDM_LINK_STATE_RX_ERROR, 0U, 0U, 0U, 0U, 0U,
                             false, "接收超时，继续监听", NULL);
            schedule_idle_return();
        }
        return_to_idle_if_due();
        vTaskDelay(OFDM_LINK_MODEM_YIELD_TICKS);
    }
}

esp_err_t ofdm_link_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_audio_queue = xQueueCreate(OFDM_LINK_AUDIO_QUEUE_LENGTH,
                                 sizeof(ofdm_audio_block_t));
    s_command_queue = xQueueCreate(OFDM_LINK_COMMAND_QUEUE_LENGTH,
                                   sizeof(ofdm_link_command_t));
    s_snapshot_mutex = xSemaphoreCreateMutex();
    s_capture_pcm = heap_caps_calloc(
        OFDM_LINK_CAPTURE_SAMPLES, sizeof(s_capture_pcm[0]),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_capture_float = heap_caps_calloc(
        OFDM_LINK_CAPTURE_SAMPLES, sizeof(s_capture_float[0]),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_frame_float = heap_caps_calloc(
        OFDM_FRAME_SAMPLE_COUNT, sizeof(s_frame_float[0]),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_transmit_pcm = heap_caps_calloc(
        OFDM_FRAME_SAMPLE_COUNT, sizeof(s_transmit_pcm[0]),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == s_audio_queue || NULL == s_command_queue ||
        NULL == s_snapshot_mutex || NULL == s_capture_pcm ||
        NULL == s_capture_float || NULL == s_frame_float ||
        NULL == s_transmit_pcm) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "OFDM_RX_CONFIG queue_blocks=%u block_frames=%u coverage_ms=%u dsp_budget_ms=%u",
             (unsigned int)OFDM_LINK_AUDIO_QUEUE_LENGTH,
             (unsigned int)OFDM_AUDIO_READ_FRAMES,
             (unsigned int)OFDM_LINK_AUDIO_QUEUE_COVERAGE_MS,
             (unsigned int)OFDM_LINK_DSP_STALL_BUDGET_MS);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_health, 0, sizeof(s_health));
    ofdm_reassembly_reset(&s_reassembly);
    ofdm_calibration_reset_stats(&s_calibration_stats);
    s_calibration_cancel_requested = false;
    s_calibration_rx_armed = false;
    s_calibration_rx_active = false;
    s_calibration_tx_active = false;
    s_calibration_command_pending = false;
    s_calibration_tail_guard = false;
    s_calibration_tail_guard_until_us = 0;
    s_calibration_deadline_us = 0;
    s_calibration_run_id = 0U;
    s_calibration_next_sequence = 0U;
    s_calibration_expected_tx = 0U;
    s_calibration_expected_rx = 0U;
    s_calibration_control_ready = false;
    reset_capture();
    s_initialized = true;
    return ESP_OK;
}

esp_err_t ofdm_link_start(void)
{
    if (!s_initialized || NULL != s_audio_task_handle ||
        NULL != s_modem_task_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t created = xTaskCreatePinnedToCore(
        audio_task, "ofdm_audio_rx", OFDM_LINK_AUDIO_TASK_STACK, NULL,
        OFDM_LINK_AUDIO_TASK_PRIORITY, &s_audio_task_handle,
        OFDM_LINK_TASK_CORE);
    if (pdPASS != created) {
        return ESP_ERR_NO_MEM;
    }
    created = xTaskCreatePinnedToCore(
        modem_task, "ofdm_modem", OFDM_LINK_MODEM_TASK_STACK, NULL,
        OFDM_LINK_MODEM_TASK_PRIORITY, &s_modem_task_handle,
        OFDM_LINK_TASK_CORE);
    return pdPASS == created ? ESP_OK : ESP_ERR_NO_MEM;
}

bool ofdm_link_request_send(void)
{
    if (!s_initialized || NULL == s_command_queue ||
        s_calibration_command_pending || s_calibration_rx_armed ||
        s_calibration_rx_active || s_calibration_tx_active) {
        return false;
    }
    ofdm_link_snapshot_t snapshot = {0};
    if (!ofdm_link_get_snapshot(&snapshot) ||
        (OFDM_LINK_STATE_IDLE_RX != snapshot.state &&
         OFDM_LINK_STATE_RX_OK != snapshot.state &&
         OFDM_LINK_STATE_RX_ERROR != snapshot.state &&
         OFDM_LINK_STATE_TX_DONE != snapshot.state)) {
        return false;
    }
    const ofdm_link_command_t command = OFDM_LINK_COMMAND_SEND;
    return pdTRUE == xQueueSend(s_command_queue, &command, 0U);
}

static bool calibration_command_state_allowed(void)
{
    ofdm_link_snapshot_t snapshot = {0};
    return ofdm_link_get_snapshot(&snapshot) &&
           (OFDM_LINK_STATE_IDLE_RX == snapshot.state ||
            OFDM_LINK_STATE_RX_OK == snapshot.state ||
            OFDM_LINK_STATE_RX_ERROR == snapshot.state ||
            OFDM_LINK_STATE_TX_DONE == snapshot.state ||
            OFDM_LINK_STATE_ERROR == snapshot.state);
}

static bool queue_calibration_command(ofdm_link_command_t command)
{
    if (!s_initialized || NULL == s_command_queue ||
        s_calibration_command_pending || s_calibration_rx_armed ||
        s_calibration_rx_active || s_calibration_tx_active ||
        !calibration_command_state_allowed()) {
        return false;
    }
    s_calibration_command_pending = true;
    s_calibration_cancel_requested = false;
    if (pdTRUE != xQueueSend(s_command_queue, &command, 0U)) {
        s_calibration_command_pending = false;
        return false;
    }
    return true;
}

bool ofdm_link_request_tx_calibration(void)
{
    return queue_calibration_command(OFDM_LINK_COMMAND_TX_CALIBRATION);
}

bool ofdm_link_request_rx_calibration(void)
{
    return queue_calibration_command(OFDM_LINK_COMMAND_RX_CALIBRATION);
}

bool ofdm_link_request_stop_calibration(void)
{
    if (!s_initialized ||
        (!s_calibration_command_pending && !s_calibration_rx_armed &&
         !s_calibration_rx_active && !s_calibration_tx_active)) {
        return false;
    }
    s_calibration_cancel_requested = true;
    return true;
}

bool ofdm_link_get_snapshot(ofdm_link_snapshot_t *snapshot)
{
    if (NULL == snapshot || NULL == s_snapshot_mutex ||
        pdTRUE != xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100U))) {
        return false;
    }
    *snapshot = s_snapshot;
    xSemaphoreGive(s_snapshot_mutex);
    return true;
}

void ofdm_link_take_health(ofdm_link_health_t *health)
{
    if (NULL == health) {
        return;
    }
    portENTER_CRITICAL(&s_health_lock);
    *health = s_health;
    s_health.rx_queue_peak = 0U;
    s_health.dsp_us_max = 0U;
    s_health.chirp_hits = 0U;
    s_health.rx_mean_square_max = 0U;
    s_health.rx_peak_max = 0U;
    s_health.rx_clip_samples = 0U;
    s_health.chirp_score_max_milli = 0U;
    portEXIT_CRITICAL(&s_health_lock);
}

const char *ofdm_link_state_name(ofdm_link_state_t state)
{
    switch (state) {
        case OFDM_LINK_STATE_BOOT:
            return "BOOT";
        case OFDM_LINK_STATE_IDLE_RX:
            return "IDLE";
        case OFDM_LINK_STATE_RX_SYNC:
            return "RX_SYNC";
        case OFDM_LINK_STATE_RX_DATA:
            return "RX_DATA";
        case OFDM_LINK_STATE_RX_OK:
            return "RX_OK";
        case OFDM_LINK_STATE_RX_ERROR:
            return "RX_ERROR";
        case OFDM_LINK_STATE_TX_DATA:
            return "TX_DATA";
        case OFDM_LINK_STATE_TX_DONE:
            return "TX_DONE";
        case OFDM_LINK_STATE_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}
