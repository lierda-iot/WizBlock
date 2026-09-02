#include "audio_processor.h"
#include "audio_opus_demo_logic.h"
#include "audio_opus_demo_ui.h"
#include "board_laiwfs300.h"
#include "opus_codec.h"
#include "board_pins.h"
#include "io_expander.h"

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio_opus_demo";

#define SAMPLE_RATE              BOARD_LAIWFS300_I2S_SAMPLE_RATE
#define RECORD_SECONDS           10U
#define RECORD_PROGRESS_TICKS    (RECORD_SECONDS * 10U)
#define TOTAL_SAMPLES            (SAMPLE_RATE * RECORD_SECONDS)
#define TOTAL_BYTES              (TOTAL_SAMPLES * sizeof(int16_t))
#define OPUS_FRAME_MS            60U
#define OPUS_BITRATE             32000
#define OPUS_COMPLEXITY          5U
#define MAX_OPUS_PACKETS         256U
#define MAX_OPUS_PKT_LEN         512U
#define OPUS_TASK_STACK          32768U
#define UI_REFRESH_TASK_STACK    4096U
#define UI_REFRESH_INTERVAL_MS   100U
#define STARTUP_DELAY_MS         6000U
#define STATUS_LOCK_TIMEOUT_MS   1000U
#define OUTPUT_VOLUME            100
#define RECORD_BEEP_HZ           1000U
#define PLAYBACK_BEEP_HZ         2000U
#define BEEP_DURATION_MS         200U
#define BEEP_CHUNK_SAMPLES       128U
#define BEEP_AMPLITUDE           12000.0f
#define TWO_PI_F                 6.28318530f
#define POST_RECORD_DELAY_MS     200U
#define PRE_PLAYBACK_DELAY_MS    300U
#define ENCODE_YIELD_FRAMES      10U

typedef struct {
    uint16_t len;
    uint8_t data[MAX_OPUS_PKT_LEN];
} opus_packet_t;

typedef struct {
    int16_t *ns_buf;
    int16_t *feed_buf;
    int16_t *tdm_buf;
    int16_t *fetch_buf;
    opus_packet_t *packets;
    int16_t *decode_buf;
} opus_cycle_buffers_t;

static SemaphoreHandle_t s_status_mutex;
static TaskHandle_t s_opus_task_handle;
static audio_opus_demo_status_t s_status = {
    .state = AUDIO_OPUS_DEMO_STATE_PREPARING,
};

static bool status_lock(void)
{
    if (NULL == s_status_mutex) {
        return false;
    }
    return pdTRUE == xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(STATUS_LOCK_TIMEOUT_MS));
}

static void status_unlock(void)
{
    if (NULL != s_status_mutex) {
        xSemaphoreGive(s_status_mutex);
    }
}

static void publish_status(audio_opus_demo_state_t state,
                           size_t completed_units,
                           size_t total_units,
                           uint32_t packet_count,
                           uint32_t raw_bytes,
                           uint32_t encoded_bytes,
                           const char *error_text)
{
    audio_opus_demo_status_t status = {
        .state = state,
        .completed_units = completed_units,
        .total_units = total_units,
        .progress_percent = audio_opus_demo_progress_percent(completed_units, total_units),
        .packet_count = packet_count,
        .raw_bytes = raw_bytes,
        .encoded_bytes = encoded_bytes,
        .compression_percent = audio_opus_demo_compression_percent(raw_bytes, encoded_bytes),
    };

    if (NULL != error_text) {
        snprintf(status.error_text, sizeof(status.error_text), "%s", error_text);
    }

    if (status_lock()) {
        s_status = status;
        status_unlock();
    }
}

static bool copy_status(audio_opus_demo_status_t *status)
{
    bool copied = false;

    if (NULL == status) {
        return false;
    }
    if (status_lock()) {
        *status = s_status;
        copied = true;
        status_unlock();
    }
    return copied;
}

static bool request_cycle(bool initial_cycle)
{
    bool accepted = false;

    if (NULL == s_opus_task_handle) {
        return false;
    }

    if (status_lock()) {
        if (initial_cycle || audio_opus_demo_restart_allowed(s_status.state)) {
            memset(&s_status, 0, sizeof(s_status));
            s_status.state = AUDIO_OPUS_DEMO_STATE_PREPARING;
            accepted = true;
        }
        status_unlock();
    }

    if (accepted) {
        xTaskNotifyGive(s_opus_task_handle);
    }
    return accepted;
}

static void format_stage_error(char *buffer, size_t buffer_len, const char *stage, esp_err_t error)
{
    if (NULL == buffer || 0U == buffer_len || NULL == stage) {
        return;
    }
    snprintf(buffer, buffer_len, "%s: %s", stage, esp_err_to_name(error));
}

static esp_err_t play_beep(esp_codec_dev_handle_t out_dev, uint32_t frequency_hz)
{
    int16_t tone[BEEP_CHUNK_SAMPLES] = {0};
    uint32_t written = 0;
    uint32_t beep_samples = (SAMPLE_RATE * BEEP_DURATION_MS) / 1000U;

    if (NULL == out_dev || 0U == frequency_hz) {
        return ESP_ERR_INVALID_ARG;
    }

    while (written < beep_samples) {
        size_t samples = beep_samples - written;

        if (BEEP_CHUNK_SAMPLES < samples) {
            samples = BEEP_CHUNK_SAMPLES;
        }
        for (size_t i = 0; i < samples; i++) {
            float time_s = (float)(written + i) / (float)SAMPLE_RATE;

            tone[i] = (int16_t)(BEEP_AMPLITUDE * sinf(TWO_PI_F * (float)frequency_hz * time_s));
        }
        if (ESP_OK != esp_codec_dev_write(out_dev, tone, samples * sizeof(int16_t))) {
            return ESP_FAIL;
        }
        written += (uint32_t)samples;
    }
    return ESP_OK;
}

static void free_cycle_buffers(opus_cycle_buffers_t *buffers)
{
    if (NULL == buffers) {
        return;
    }

    heap_caps_free(buffers->ns_buf);
    heap_caps_free(buffers->feed_buf);
    heap_caps_free(buffers->tdm_buf);
    heap_caps_free(buffers->fetch_buf);
    heap_caps_free(buffers->packets);
    heap_caps_free(buffers->decode_buf);
    memset(buffers, 0, sizeof(*buffers));
}

static void run_opus_cycle(void)
{
    const audio_processor_config_t afe_config = {
        .mic_channels = 1,
        .ref_channels = 0,
        .enable_ns = true,
        .enable_aec = false,
        .enable_vad = true,
    };
    const opus_encoder_config_t encoder_config = {
        .sample_rate = SAMPLE_RATE,
        .channels = 1,
        .frame_duration_ms = OPUS_FRAME_MS,
        .bitrate = OPUS_BITRATE,
        .complexity = OPUS_COMPLEXITY,
        .enable_vbr = true,
        .enable_dtx = false,
    };
    const opus_decoder_config_t decoder_config = {
        .sample_rate = SAMPLE_RATE,
        .channels = 1,
        .frame_duration_ms = OPUS_FRAME_MS,
    };
    opus_cycle_buffers_t buffers = {0};
    esp_codec_dev_handle_t out_dev = NULL;
    size_t feed_chunk = 0;
    size_t fetch_chunk = 0;
    size_t opus_frame_samples = 0;
    size_t ns_offset = 0;
    size_t raw_fed = 0;
    size_t packet_count = 0;
    size_t encoded_offset = 0;
    size_t frame_total = 0;
    uint32_t total_encoded_bytes = 0;
    uint32_t raw_bytes = 0;
    bool afe_initialized = false;
    bool opus_initialized = false;
    bool amp_enabled = false;
    bool cycle_complete = false;
    char error_text[AUDIO_OPUS_DEMO_ERROR_TEXT_LEN] = {0};
    esp_err_t ret = ESP_OK;

    publish_status(AUDIO_OPUS_DEMO_STATE_PREPARING, 0U, 0U, 0U, 0U, 0U, NULL);
    ESP_LOGI(TAG, "initializing audio cycle...");

    ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Audio init", ret);
        goto cleanup;
    }

    ret = audio_processor_init(&afe_config);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "AFE init", ret);
        goto cleanup;
    }
    afe_initialized = true;

    ret = opus_codec_encoder_init(&encoder_config);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Opus encoder", ret);
        goto cleanup;
    }
    opus_initialized = true;

    ret = opus_codec_decoder_init(&decoder_config);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Opus decoder", ret);
        goto cleanup;
    }

    feed_chunk = audio_processor_get_feed_chunksize();
    fetch_chunk = audio_processor_get_fetch_chunksize();
    opus_frame_samples = opus_codec_encoder_frame_samples();
    if (0U == feed_chunk || 0U == fetch_chunk || 0U == opus_frame_samples) {
        snprintf(error_text, sizeof(error_text), "Invalid audio frame sizes");
        goto cleanup;
    }
    ESP_LOGI(TAG, "AFE feed=%u fetch=%u, Opus frame=%u samples",
             (unsigned)feed_chunk, (unsigned)fetch_chunk, (unsigned)opus_frame_samples);

    buffers.ns_buf = heap_caps_malloc(TOTAL_BYTES, MALLOC_CAP_SPIRAM);
    buffers.feed_buf = heap_caps_malloc(feed_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    buffers.tdm_buf = heap_caps_malloc(feed_chunk * 4U * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    buffers.fetch_buf = heap_caps_malloc(fetch_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    buffers.packets = heap_caps_malloc(MAX_OPUS_PACKETS * sizeof(opus_packet_t), MALLOC_CAP_SPIRAM);
    buffers.decode_buf = heap_caps_malloc(opus_frame_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (NULL == buffers.ns_buf || NULL == buffers.feed_buf || NULL == buffers.tdm_buf ||
        NULL == buffers.fetch_buf || NULL == buffers.packets || NULL == buffers.decode_buf) {
        snprintf(error_text, sizeof(error_text), "PSRAM allocation failed");
        goto cleanup;
    }

    out_dev = board_laiwfs300_audio_get_output_dev();
    if (NULL == out_dev) {
        snprintf(error_text, sizeof(error_text), "Audio output unavailable");
        goto cleanup;
    }

    ret = io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                        BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "AMP direction", ret);
        goto cleanup;
    }
    ret = io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "AMP enable", ret);
        goto cleanup;
    }
    amp_enabled = true;

    ret = esp_codec_dev_set_out_vol(out_dev, OUTPUT_VOLUME);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Output volume", ret);
        goto cleanup;
    }

    ret = play_beep(out_dev, RECORD_BEEP_HZ);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Record beep", ret);
        goto cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(POST_RECORD_DELAY_MS));

    ESP_LOGI(TAG, "RECORDING 10s with NS...");
    publish_status(AUDIO_OPUS_DEMO_STATE_RECORDING, 0U, RECORD_PROGRESS_TICKS,
                   0U, 0U, 0U, NULL);

    {
        const size_t feeds_per_fetch = (fetch_chunk + feed_chunk - 1U) / feed_chunk;

        while (raw_fed < TOTAL_SAMPLES) {
            for (size_t feed_index = 0; feed_index < feeds_per_fetch && raw_fed < TOTAL_SAMPLES; feed_index++) {
                ret = board_laiwfs300_audio_read_tdm_4ch(buffers.tdm_buf, feed_chunk);
                if (ESP_OK != ret) {
                    format_stage_error(error_text, sizeof(error_text), "Audio read", ret);
                    goto cleanup;
                }
                for (size_t i = 0; i < feed_chunk; i++) {
                    buffers.feed_buf[i] = buffers.tdm_buf[i * 4U];
                }
                ret = audio_processor_feed(buffers.feed_buf, feed_chunk);
                if (ESP_OK != ret) {
                    ESP_LOGW(TAG, "AFE feed failed: %s", esp_err_to_name(ret));
                }
                raw_fed += feed_chunk;
            }

            {
                size_t fetched = 0;
                bool vad_active = false;

                if (ESP_OK == audio_processor_fetch(buffers.fetch_buf, &fetched, &vad_active, NULL)) {
                    size_t copy_samples = fetched;

                    if (ns_offset + copy_samples > TOTAL_SAMPLES) {
                        copy_samples = TOTAL_SAMPLES - ns_offset;
                    }
                    memcpy(&buffers.ns_buf[ns_offset], buffers.fetch_buf,
                           copy_samples * sizeof(int16_t));
                    ns_offset += copy_samples;
                }
            }

            {
                size_t progress_ticks = (raw_fed * 10U) / SAMPLE_RATE;

                if (RECORD_PROGRESS_TICKS < progress_ticks) {
                    progress_ticks = RECORD_PROGRESS_TICKS;
                }
                publish_status(AUDIO_OPUS_DEMO_STATE_RECORDING, progress_ticks,
                               RECORD_PROGRESS_TICKS, 0U, 0U, 0U, NULL);
            }
        }
    }

    ESP_LOGI(TAG, "Recorded %u NS samples. Encoding with Opus...", (unsigned)ns_offset);
    if (ns_offset < opus_frame_samples) {
        snprintf(error_text, sizeof(error_text), "No complete audio frame");
        goto cleanup;
    }

    frame_total = ns_offset / opus_frame_samples;
    if (MAX_OPUS_PACKETS < frame_total) {
        frame_total = MAX_OPUS_PACKETS;
    }
    publish_status(AUDIO_OPUS_DEMO_STATE_ENCODING, 0U, frame_total, 0U, 0U, 0U, NULL);

    while (packet_count < frame_total) {
        size_t out_len = MAX_OPUS_PKT_LEN;

        ret = opus_codec_encode(&buffers.ns_buf[encoded_offset], opus_frame_samples,
                                buffers.packets[packet_count].data, &out_len);
        if (ESP_OK != ret) {
            format_stage_error(error_text, sizeof(error_text), "Opus encode", ret);
            goto cleanup;
        }
        buffers.packets[packet_count].len = (uint16_t)out_len;
        total_encoded_bytes += (uint32_t)out_len;
        packet_count++;
        encoded_offset += opus_frame_samples;
        raw_bytes = (uint32_t)(encoded_offset * sizeof(int16_t));
        publish_status(AUDIO_OPUS_DEMO_STATE_ENCODING, packet_count, frame_total,
                       (uint32_t)packet_count, raw_bytes, total_encoded_bytes, NULL);
        if (0U == (packet_count % ENCODE_YIELD_FRAMES)) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    ESP_LOGI(TAG, "Opus encoded: %u frames, raw=%lu bytes -> encoded=%lu bytes (%lu%% compression)",
             (unsigned)packet_count, (unsigned long)raw_bytes, (unsigned long)total_encoded_bytes,
             (unsigned long)audio_opus_demo_compression_percent(raw_bytes, total_encoded_bytes));

    publish_status(AUDIO_OPUS_DEMO_STATE_PLAYING, 0U, packet_count,
                   (uint32_t)packet_count, raw_bytes, total_encoded_bytes, NULL);
    ret = play_beep(out_dev, PLAYBACK_BEEP_HZ);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Playback beep", ret);
        goto cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(PRE_PLAYBACK_DELAY_MS));

    ESP_LOGI(TAG, "PLAYING Opus decoded audio...");
    for (size_t i = 0; i < packet_count; i++) {
        size_t dec_samples = opus_frame_samples;

        ret = opus_codec_decode(buffers.packets[i].data, buffers.packets[i].len,
                                buffers.decode_buf, &dec_samples);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "Decode fail at frame %u", (unsigned)i);
            continue;
        }
        ret = esp_codec_dev_write(out_dev, buffers.decode_buf, dec_samples * sizeof(int16_t));
        if (ESP_OK != ret) {
            format_stage_error(error_text, sizeof(error_text), "Audio playback", ret);
            goto cleanup;
        }
        publish_status(AUDIO_OPUS_DEMO_STATE_PLAYING, i + 1U, packet_count,
                       (uint32_t)packet_count, raw_bytes, total_encoded_bytes, NULL);
    }

    ESP_LOGI(TAG, "DONE");
    cycle_complete = true;

cleanup:
    if (amp_enabled) {
        esp_err_t amp_ret = io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                                  BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, false);
        if (ESP_OK != amp_ret) {
            ESP_LOGW(TAG, "AMP disable failed: %s", esp_err_to_name(amp_ret));
        }
    }
    free_cycle_buffers(&buffers);
    if (opus_initialized) {
        opus_codec_deinit();
    }
    if (afe_initialized) {
        audio_processor_deinit();
    }

    if (cycle_complete) {
        publish_status(AUDIO_OPUS_DEMO_STATE_COMPLETE, packet_count, packet_count,
                       (uint32_t)packet_count, raw_bytes, total_encoded_bytes, NULL);
    } else {
        if ('\0' == error_text[0]) {
            snprintf(error_text, sizeof(error_text), "Audio cycle failed");
        }
        ESP_LOGE(TAG, "%s", error_text);
        publish_status(AUDIO_OPUS_DEMO_STATE_ERROR, 0U, 0U,
                       (uint32_t)packet_count, raw_bytes, total_encoded_bytes, error_text);
    }
}

static void opus_worker_task(void *arg)
{
    bool first_cycle = true;

    (void)arg;

    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (first_cycle) {
            ESP_LOGI(TAG, "waiting 6s for serial connection...");
            vTaskDelay(pdMS_TO_TICKS(STARTUP_DELAY_MS));
            first_cycle = false;
        }
        run_opus_cycle();
    }
}

static void ui_refresh_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)arg;

    while (true) {
        audio_opus_demo_status_t status = {0};

        if (copy_status(&status)) {
            (void)audio_opus_demo_ui_update(&status);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UI_REFRESH_INTERVAL_MS));
    }
}

static void on_restart_requested(void *user_ctx)
{
    (void)user_ctx;

    if (request_cycle(false)) {
        ESP_LOGI(TAG, "new record/play cycle requested");
    }
}

void app_main(void)
{
    const audio_opus_demo_ui_callbacks_t ui_callbacks = {
        .on_restart = on_restart_requested,
        .user_ctx = NULL,
    };
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Audio Opus Demo: NS + Opus encode/decode");

    ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    s_status_mutex = xSemaphoreCreateMutex();
    if (NULL == s_status_mutex) {
        ESP_LOGE(TAG, "status mutex create failed");
        return;
    }

    ret = audio_opus_demo_ui_init(&ui_callbacks);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "UI init failed: %s", esp_err_to_name(ret));
        return;
    }

    task_ok = xTaskCreate(opus_worker_task, "opus_test", OPUS_TASK_STACK, NULL,
                          tskIDLE_PRIORITY + 3, &s_opus_task_handle);
    if (pdPASS != task_ok) {
        ESP_LOGE(TAG, "audio worker task create failed");
        return;
    }

    task_ok = xTaskCreate(ui_refresh_task, "opus_ui_sync", UI_REFRESH_TASK_STACK, NULL,
                          tskIDLE_PRIORITY + 2, NULL);
    if (pdPASS != task_ok) {
        ESP_LOGE(TAG, "UI refresh task create failed");
        return;
    }

    if (!request_cycle(true)) {
        ESP_LOGE(TAG, "initial audio cycle request failed");
        return;
    }

    ESP_LOGI(TAG, "Audio Opus Demo ready");
}
