#pragma once

#include "esp_err.h"
#include "companion_audio_vad_policy.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMPANION_AUDIO_SAMPLE_RATE_HZ 16000U
#define COMPANION_AUDIO_TDM_CHANNELS 4U
#define COMPANION_AUDIO_SNAPSHOT_FRAMES 16384U
#define COMPANION_AUDIO_MIC1_SLOT 0U
#define COMPANION_AUDIO_MIC2_SLOT 2U
#define COMPANION_AUDIO_OPUS_FRAME_MS 60U
#define COMPANION_AUDIO_OPUS_BITRATE 17000U
#define COMPANION_AUDIO_OUTPUT_VOLUME 90U
#define COMPANION_AUDIO_PLAYBACK_QUEUE_DEPTH 32U

typedef enum {
    COMPANION_AUDIO_EVENT_WAKE = 0,
    COMPANION_AUDIO_EVENT_VAD_START,
    COMPANION_AUDIO_EVENT_SPEECH_CONFIRMED,
    COMPANION_AUDIO_EVENT_VAD_END,
    COMPANION_AUDIO_EVENT_ERROR,
    COMPANION_AUDIO_EVENT_FATAL,
} companion_audio_event_type_t;

typedef struct {
    companion_audio_event_type_t type;
    uint32_t generation;
    uint32_t wake_seq;
    esp_err_t result;
} companion_audio_event_t;

typedef struct {
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t session_epoch;
    uint32_t request_id;
} companion_audio_token_t;

typedef esp_err_t (*companion_audio_reserve_wake_cb_t)(uint32_t *generation,
                                                       uint32_t *wake_seq,
                                                       void *user_ctx);
typedef void (*companion_audio_event_cb_t)(const companion_audio_event_t *event,
                                          void *user_ctx);
typedef void (*companion_audio_opus_cb_t)(const uint8_t *data, int length,
                                         const companion_audio_token_t *token,
                                         void *user_ctx);
typedef bool (*companion_audio_upload_token_cb_t)(
    companion_audio_token_t *token, void *user_ctx);

typedef struct {
    companion_audio_reserve_wake_cb_t reserve_wake;
    companion_audio_event_cb_t on_event;
    companion_audio_opus_cb_t on_opus;
    companion_audio_upload_token_cb_t get_upload_token;
    void *user_ctx;
} companion_audio_config_t;

typedef enum {
    COMPANION_AUDIO_OUTPUT_SILENT = 0,
    COMPANION_AUDIO_OUTPUT_PROMPT,
    COMPANION_AUDIO_OUTPUT_TTS,
} companion_audio_output_owner_t;

typedef enum {
    COMPANION_AUDIO_OUTPUT_IDLE = 0,
    COMPANION_AUDIO_OUTPUT_ACTIVE,
    COMPANION_AUDIO_OUTPUT_STOPPING,
} companion_audio_output_phase_t;

typedef struct {
    uint32_t feed_blocks;
    uint32_t fetch_blocks;
    uint32_t encoded_frames;
    uint32_t read_errors;
    uint32_t playback_drops;
    uint32_t pcm_queue_depth;
    uint32_t pcm_queue_high_water;
    uint32_t pcm_queue_drops;
    uint32_t encode_gate_drops;
    uint32_t encode_errors;
    uint32_t encode_max_us;
    uint32_t snapshot_version;
    companion_audio_output_owner_t output_owner;
    companion_audio_output_phase_t output_phase;
    companion_audio_token_t output_token;
} companion_audio_stats_t;

esp_err_t companion_audio_init(const companion_audio_config_t *config);
esp_err_t companion_audio_start(void);
esp_err_t companion_audio_stop(void);
esp_err_t companion_audio_stop_ex(uint32_t timeout_ms);
esp_err_t companion_audio_copy_snapshot(uint32_t wake_seq, int16_t *mic1,
                                        int16_t *mic2, size_t frame_capacity,
                                        uint32_t *snapshot_version);
esp_err_t companion_audio_play_opus(const uint8_t *data, int length);
esp_err_t companion_audio_play_opus_ex(const uint8_t *data, int length,
                                       const companion_audio_token_t *token);
esp_err_t companion_audio_play_stop(void);
esp_err_t companion_audio_play_stop_ex(
    const companion_audio_token_t *token);
esp_err_t companion_audio_prompt_play(const char *url);
esp_err_t companion_audio_prompt_play_ex(
    const char *url, const companion_audio_token_t *token);
esp_err_t companion_audio_prompt_stop(void);
void companion_audio_get_stats(companion_audio_stats_t *stats);
