#include "audio_ns_demo_logic.h"
#include "audio_ns_demo_ui.h"
#include "audio_processor.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "io_expander.h"
#include "launcher_return.h"

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio_ns_demo";

#define SAMPLE_RATE                 BOARD_LAIWFS300_I2S_SAMPLE_RATE
#define RECORD_SECONDS              10U
#define MILLISECONDS_PER_SECOND     1000U
#define RECORD_DURATION_MS          (RECORD_SECONDS * MILLISECONDS_PER_SECOND)
#define TOTAL_SAMPLES               (SAMPLE_RATE * RECORD_SECONDS)
#define TOTAL_BYTES                 (TOTAL_SAMPLES * sizeof(int16_t))
#define TDM_CHANNEL_COUNT           4U
#define AUDIO_TASK_STACK            8192U
#define UI_REFRESH_TASK_STACK       4096U
#define UI_REFRESH_INTERVAL_MS      100U
#define STARTUP_DELAY_MS            6000U
#define STATUS_LOCK_TIMEOUT_MS      1000U
#define INPUT_GAIN_DB               30.0f
#define OUTPUT_VOLUME               90
#define RECORD_BEEP_HZ              1000U
#define RAW_PLAYBACK_BEEP_HZ        1500U
#define DENOISED_PLAYBACK_BEEP_HZ   2000U
#define BEEP_DURATION_MS            200U
#define BEEP_CHUNK_SAMPLES          128U
#define BEEP_AMPLITUDE              12000.0f
#define TWO_PI_F                    6.28318530f
#define POST_RECORD_BEEP_DELAY_MS   200U
#define PRE_PLAYBACK_DELAY_MS       300U
#define PLAYBACK_CHUNK_SAMPLES      512U

typedef struct {
    int16_t *raw_buf;
    int16_t *ns_buf;
    int16_t *feed_buf;
    int16_t *tdm_buf;
    int16_t *fetch_buf;
} audio_ns_cycle_buffers_t;

static SemaphoreHandle_t s_status_mutex;
static TaskHandle_t s_audio_task_handle;
static audio_ns_demo_status_t s_status = {
    .state = AUDIO_NS_DEMO_STATE_PREPARING,
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

static void publish_status(audio_ns_demo_state_t state,
                           size_t completed_ms,
                           size_t total_ms,
                           uint32_t raw_rms,
                           uint32_t denoised_rms,
                           bool rms_ready,
                           const char *error_text)
{
    audio_ns_demo_status_t status = {
        .state = state,
        .completed_ms = completed_ms,
        .total_ms = total_ms,
        .progress_percent = audio_ns_demo_progress_percent(completed_ms, total_ms),
        .raw_rms = raw_rms,
        .denoised_rms = denoised_rms,
        .rms_ready = rms_ready,
    };

    if (NULL != error_text) {
        snprintf(status.error_text, sizeof(status.error_text), "%s", error_text);
    }

    if (status_lock()) {
        s_status = status;
        status_unlock();
    }
}

static bool copy_status(audio_ns_demo_status_t *status)
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

    if (NULL == s_audio_task_handle) {
        return false;
    }

    if (status_lock()) {
        if (initial_cycle || audio_ns_demo_restart_allowed(s_status.state)) {
            memset(&s_status, 0, sizeof(s_status));
            s_status.state = AUDIO_NS_DEMO_STATE_PREPARING;
            accepted = true;
        }
        status_unlock();
    }

    if (accepted) {
        xTaskNotifyGive(s_audio_task_handle);
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

static size_t samples_to_milliseconds(size_t samples)
{
    return (samples * MILLISECONDS_PER_SECOND) / SAMPLE_RATE;
}

static uint32_t calculate_rms(const int16_t *samples, size_t sample_count)
{
    int64_t square_sum = 0;

    if (NULL == samples || 0U == sample_count) {
        return 0U;
    }

    for (size_t i = 0; i < sample_count; i++) {
        square_sum += (int64_t)samples[i] * samples[i];
    }
    return (uint32_t)sqrtf((float)square_sum / (float)sample_count);
}

static esp_err_t play_beep(esp_codec_dev_handle_t out_dev, uint32_t frequency_hz)
{
    int16_t tone[BEEP_CHUNK_SAMPLES] = {0};
    uint32_t written = 0;
    uint32_t beep_samples = (SAMPLE_RATE * BEEP_DURATION_MS) / MILLISECONDS_PER_SECOND;

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

static esp_err_t play_samples(esp_codec_dev_handle_t out_dev,
                              const int16_t *samples,
                              size_t sample_count,
                              audio_ns_demo_state_t state,
                              uint32_t raw_rms,
                              uint32_t denoised_rms)
{
    size_t played = 0;
    size_t total_ms = samples_to_milliseconds(sample_count);

    if (NULL == out_dev || NULL == samples || 0U == sample_count) {
        return ESP_ERR_INVALID_ARG;
    }

    while (played < sample_count) {
        size_t chunk_samples = sample_count - played;
        esp_err_t ret = ESP_OK;

        if (PLAYBACK_CHUNK_SAMPLES < chunk_samples) {
            chunk_samples = PLAYBACK_CHUNK_SAMPLES;
        }
        ret = esp_codec_dev_write(out_dev, (void *)&samples[played],
                                  chunk_samples * sizeof(int16_t));
        if (ESP_OK != ret) {
            return ret;
        }
        played += chunk_samples;
        publish_status(state,
                       samples_to_milliseconds(played),
                       total_ms,
                       raw_rms,
                       denoised_rms,
                       true,
                       NULL);
    }
    return ESP_OK;
}

static void free_cycle_buffers(audio_ns_cycle_buffers_t *buffers)
{
    if (NULL == buffers) {
        return;
    }

    heap_caps_free(buffers->raw_buf);
    heap_caps_free(buffers->ns_buf);
    heap_caps_free(buffers->feed_buf);
    heap_caps_free(buffers->tdm_buf);
    heap_caps_free(buffers->fetch_buf);
    memset(buffers, 0, sizeof(*buffers));
}

static void run_ns_cycle(void)
{
    const audio_processor_config_t afe_config = {
        .mic_channels = 1,
        .ref_channels = 0,
        .enable_ns = true,
        .enable_aec = false,
        .enable_vad = true,
    };
    audio_ns_cycle_buffers_t buffers = {0};
    esp_codec_dev_handle_t in_dev = NULL;
    esp_codec_dev_handle_t out_dev = NULL;
    size_t feed_chunk = 0;
    size_t fetch_chunk = 0;
    size_t raw_offset = 0;
    size_t ns_offset = 0;
    uint32_t raw_rms = 0;
    uint32_t denoised_rms = 0;
    bool afe_initialized = false;
    bool amp_enabled = false;
    bool rms_ready = false;
    bool cycle_complete = false;
    char error_text[AUDIO_NS_DEMO_ERROR_TEXT_LEN] = {0};
    esp_err_t ret = ESP_OK;

    publish_status(AUDIO_NS_DEMO_STATE_PREPARING, 0U, 0U, 0U, 0U, false, NULL);
    ESP_LOGI(TAG, "initializing audio cycle...");

    ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Audio init", ret);
        goto cleanup;
    }

    in_dev = board_laiwfs300_audio_get_input_dev();
    if (NULL == in_dev) {
        snprintf(error_text, sizeof(error_text), "Audio input unavailable");
        goto cleanup;
    }

    ret = esp_codec_dev_set_in_channel_gain(in_dev,
                                             ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
                                             INPUT_GAIN_DB);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Input gain", ret);
        goto cleanup;
    }
    ESP_LOGI(TAG, "MIC1 input gain set to %.1f dB", INPUT_GAIN_DB);

    ret = audio_processor_init(&afe_config);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "AFE init", ret);
        goto cleanup;
    }
    afe_initialized = true;

    feed_chunk = audio_processor_get_feed_chunksize();
    fetch_chunk = audio_processor_get_fetch_chunksize();
    if (0U == feed_chunk || 0U == fetch_chunk) {
        snprintf(error_text, sizeof(error_text), "Invalid AFE frame sizes");
        goto cleanup;
    }
    ESP_LOGI(TAG, "AFE ready, feed=%u fetch=%u", (unsigned)feed_chunk, (unsigned)fetch_chunk);

    buffers.raw_buf = heap_caps_malloc(TOTAL_BYTES, MALLOC_CAP_SPIRAM);
    buffers.ns_buf = heap_caps_malloc(TOTAL_BYTES, MALLOC_CAP_SPIRAM);
    buffers.feed_buf = heap_caps_malloc(feed_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    buffers.tdm_buf = heap_caps_malloc(feed_chunk * TDM_CHANNEL_COUNT * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    buffers.fetch_buf = heap_caps_malloc(fetch_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (NULL == buffers.raw_buf || NULL == buffers.ns_buf || NULL == buffers.feed_buf ||
        NULL == buffers.tdm_buf || NULL == buffers.fetch_buf) {
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
    vTaskDelay(pdMS_TO_TICKS(POST_RECORD_BEEP_DELAY_MS));

    ESP_LOGI(TAG, "RECORDING 10s...");
    publish_status(AUDIO_NS_DEMO_STATE_RECORDING, 0U, RECORD_DURATION_MS, 0U, 0U, false, NULL);

    {
        const size_t feeds_per_fetch = (fetch_chunk + feed_chunk - 1U) / feed_chunk;

        while (raw_offset < TOTAL_SAMPLES) {
            for (size_t feed_index = 0;
                 feed_index < feeds_per_fetch && raw_offset < TOTAL_SAMPLES;
                 feed_index++) {
                size_t copy_samples = feed_chunk;

                ret = board_laiwfs300_audio_read_tdm_4ch(buffers.tdm_buf, feed_chunk);
                if (ESP_OK != ret) {
                    format_stage_error(error_text, sizeof(error_text), "Audio read", ret);
                    goto cleanup;
                }
                for (size_t i = 0; i < feed_chunk; i++) {
                    buffers.feed_buf[i] = buffers.tdm_buf[i * TDM_CHANNEL_COUNT];
                }
                if (raw_offset + copy_samples > TOTAL_SAMPLES) {
                    copy_samples = TOTAL_SAMPLES - raw_offset;
                }
                memcpy(&buffers.raw_buf[raw_offset], buffers.feed_buf,
                       copy_samples * sizeof(int16_t));
                raw_offset += copy_samples;

                ret = audio_processor_feed(buffers.feed_buf, feed_chunk);
                if (ESP_OK != ret) {
                    ESP_LOGW(TAG, "AFE feed failed: %s", esp_err_to_name(ret));
                }
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

            publish_status(AUDIO_NS_DEMO_STATE_RECORDING,
                           samples_to_milliseconds(raw_offset),
                           RECORD_DURATION_MS,
                           0U,
                           0U,
                           false,
                           NULL);
        }
    }

    ESP_LOGI(TAG, "recorded raw=%u ns=%u samples", (unsigned)raw_offset, (unsigned)ns_offset);
    if (0U == raw_offset || 0U == ns_offset) {
        snprintf(error_text, sizeof(error_text), "No audio samples recorded");
        goto cleanup;
    }

    raw_rms = calculate_rms(buffers.raw_buf, raw_offset);
    denoised_rms = calculate_rms(buffers.ns_buf, ns_offset);
    rms_ready = true;
    ESP_LOGI(TAG, "RMS raw=%lu denoised=%lu",
             (unsigned long)raw_rms, (unsigned long)denoised_rms);

    publish_status(AUDIO_NS_DEMO_STATE_PLAYING_RAW, 0U,
                   samples_to_milliseconds(raw_offset), raw_rms, denoised_rms, true, NULL);
    ret = play_beep(out_dev, RAW_PLAYBACK_BEEP_HZ);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Raw playback beep", ret);
        goto cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(PRE_PLAYBACK_DELAY_MS));
    ESP_LOGI(TAG, "PLAYING RAW...");
    ret = play_samples(out_dev, buffers.raw_buf, raw_offset,
                       AUDIO_NS_DEMO_STATE_PLAYING_RAW, raw_rms, denoised_rms);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "Raw playback", ret);
        goto cleanup;
    }

    publish_status(AUDIO_NS_DEMO_STATE_PLAYING_DENOISED, 0U,
                   samples_to_milliseconds(ns_offset), raw_rms, denoised_rms, true, NULL);
    ret = play_beep(out_dev, DENOISED_PLAYBACK_BEEP_HZ);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "NS playback beep", ret);
        goto cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(PRE_PLAYBACK_DELAY_MS));
    ESP_LOGI(TAG, "PLAYING DENOISED...");
    ret = play_samples(out_dev, buffers.ns_buf, ns_offset,
                       AUDIO_NS_DEMO_STATE_PLAYING_DENOISED, raw_rms, denoised_rms);
    if (ESP_OK != ret) {
        format_stage_error(error_text, sizeof(error_text), "NS playback", ret);
        goto cleanup;
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
    if (afe_initialized) {
        audio_processor_deinit();
    }

    if (cycle_complete) {
        publish_status(AUDIO_NS_DEMO_STATE_COMPLETE, 1U, 1U,
                       raw_rms, denoised_rms, true, NULL);
    } else {
        if ('\0' == error_text[0]) {
            snprintf(error_text, sizeof(error_text), "Audio cycle failed");
        }
        ESP_LOGE(TAG, "%s", error_text);
        publish_status(AUDIO_NS_DEMO_STATE_ERROR, 0U, 0U,
                       raw_rms, denoised_rms, rms_ready, error_text);
    }
}

static void audio_worker_task(void *arg)
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
        run_ns_cycle();
    }
}

static void ui_refresh_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)arg;

    while (true) {
        audio_ns_demo_status_t status = {0};

        if (copy_status(&status)) {
            (void)audio_ns_demo_ui_update(&status);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UI_REFRESH_INTERVAL_MS));
    }
}

static void on_restart_requested(void *user_ctx)
{
    (void)user_ctx;

    if (request_cycle(false)) {
        ESP_LOGI(TAG, "new NS comparison cycle requested");
    }
}

void app_main(void)
{
    const audio_ns_demo_ui_callbacks_t ui_callbacks = {
        .on_restart = on_restart_requested,
        .user_ctx = NULL,
    };
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Audio NS Demo");

    ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = launcher_return_start_default();
    if (ESP_OK != ret && ESP_ERR_NOT_SUPPORTED != ret) {
        ESP_LOGW(TAG, "launcher return unavailable: %s", esp_err_to_name(ret));
    }

    s_status_mutex = xSemaphoreCreateMutex();
    if (NULL == s_status_mutex) {
        ESP_LOGE(TAG, "status mutex create failed");
        return;
    }

    ret = audio_ns_demo_ui_init(&ui_callbacks);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "UI init failed: %s", esp_err_to_name(ret));
        return;
    }

    task_ok = xTaskCreate(audio_worker_task, "ns_test", AUDIO_TASK_STACK, NULL,
                          tskIDLE_PRIORITY + 3, &s_audio_task_handle);
    if (pdPASS != task_ok) {
        ESP_LOGE(TAG, "audio worker task create failed");
        return;
    }

    task_ok = xTaskCreate(ui_refresh_task, "ns_ui_sync", UI_REFRESH_TASK_STACK, NULL,
                          tskIDLE_PRIORITY + 2, NULL);
    if (pdPASS != task_ok) {
        ESP_LOGE(TAG, "UI refresh task create failed");
        return;
    }

    if (!request_cycle(true)) {
        ESP_LOGE(TAG, "initial NS comparison cycle request failed");
        return;
    }

    ESP_LOGI(TAG, "Audio NS Demo ready");
}
