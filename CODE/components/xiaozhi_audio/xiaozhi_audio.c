#include "xiaozhi_audio.h"
#include "board_laiwfs300.h"
#include "audio_processor.h"
#include "opus_codec.h"
#include "board_pins.h"
#include "bus_i2c.h"
#include "io_expander.h"

#include "esp_audio_simple_player_advance.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <string.h>
#include <math.h>

static const char *TAG = "xiaozhi_audio";

#define SAMPLE_RATE       16000
#define TDM_CHANNELS      4
#define OPUS_FRAME_MS     60
#define OPUS_SAMPLE_RATE  16000
#define OPUS_FRAME_SAMPLES (OPUS_SAMPLE_RATE * OPUS_FRAME_MS / 1000)
#define OPUS_DEC_SAMPLE_RATE  24000
#define OPUS_DEC_FRAME_SAMPLES (OPUS_DEC_SAMPLE_RATE * OPUS_FRAME_MS / 1000)
#define PLAYBACK_SAMPLE_RATE  SAMPLE_RATE
#define PLAYBACK_FRAME_SAMPLES (PLAYBACK_SAMPLE_RATE * OPUS_FRAME_MS / 1000)
#define OUTPUT_VOL        50

#define PLAYBACK_QUEUE_DEPTH   32
#define PLAYBACK_TASK_STACK    (40 * 1024)
#define PLAYBACK_TASK_PRIO     5

#define MIN_VAD_ACTIVE_FETCHES 30

#define PROMPT_FADE_IN_SAMPLES  240
#define PROMPT_PREROLL_SAMPLES  240
#define PROMPT_PREROLL_WRITES   1

typedef enum {
    PROMPT_STATE_IDLE = 0,
    PROMPT_STATE_PLAYING,
    PROMPT_STATE_CLOSED,
} prompt_state_t;

typedef struct {
    uint8_t *data;
    int len;
} opus_frame_item_t;

static xiaozhi_audio_config_t s_audio_cfg;
static esp_codec_dev_handle_t s_out_dev;
static size_t s_feed_chunk;
static size_t s_fetch_chunk;
static volatile bool s_running;
static volatile bool s_playing;
static volatile bool s_was_speaking;

static QueueHandle_t s_playback_queue;
static TaskHandle_t s_playback_task;

static esp_asp_handle_t s_prompt_player;
static SemaphoreHandle_t s_prompt_lock;
static volatile prompt_state_t s_prompt_state;
static int s_prompt_fade_in_remaining;

static void playback_task(void *arg);
static esp_err_t prompt_player_init(void);

static size_t resample_tts_to_playback_rate(const int16_t *src, size_t src_samples,
                                            int16_t *dst, size_t dst_capacity)
{
    if (NULL == src || NULL == dst || 0 == src_samples || 0 == dst_capacity) {
        return 0;
    }

    size_t dst_samples = (src_samples * PLAYBACK_SAMPLE_RATE) / OPUS_DEC_SAMPLE_RATE;
    if (dst_samples > dst_capacity) {
        dst_samples = dst_capacity;
    }

    for (size_t i = 0; i < dst_samples; ++i) {
        uint64_t src_pos_q16 = (((uint64_t)i * OPUS_DEC_SAMPLE_RATE) << 16) / PLAYBACK_SAMPLE_RATE;
        size_t src_index = (size_t)(src_pos_q16 >> 16);
        uint32_t frac = (uint32_t)(src_pos_q16 & 0xFFFFU);

        if (src_index >= src_samples) {
            src_index = src_samples - 1;
            frac = 0;
        }

        int32_t s0 = src[src_index];
        int32_t s1 = (src_index + 1 < src_samples) ? src[src_index + 1] : s0;
        int32_t sample = s0 + (int32_t)(((int64_t)(s1 - s0) * (int64_t)frac) >> 16);
        dst[i] = (int16_t)sample;
    }

    return dst_samples;
}

static void amp_enable(void)
{
    io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                  BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                          BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
}

static void feed_task(void *arg)
{
    (void)arg;
    const size_t tdm_frames = s_feed_chunk;
    int16_t *tdm_buf = heap_caps_malloc(tdm_frames * TDM_CHANNELS * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM);
    int16_t *feed_buf = heap_caps_malloc(s_feed_chunk * 2 * sizeof(int16_t),
                                          MALLOC_CAP_SPIRAM);
    if (NULL == tdm_buf || NULL == feed_buf) {
        ESP_LOGE(TAG, "feed_task alloc failed");
        vTaskDelete(NULL);
        return;
    }

    uint32_t feed_count = 0;

    while (s_running) {
        esp_err_t ret = board_laiwfs300_audio_read_tdm_4ch(tdm_buf, tdm_frames);
        if (ESP_OK != ret) {
            if (1 == feed_count || 0 == (feed_count % 50)) {
                ESP_LOGW(TAG, "feed: read_tdm FAILED ret=%d count=%lu", ret, (unsigned long)feed_count);
            }
            vTaskDelay(1);
            continue;
        }

        for (size_t i = 0; i < tdm_frames; i++) {
            int16_t mic1 = tdm_buf[i * TDM_CHANNELS + 0];
            int16_t mic2 = tdm_buf[i * TDM_CHANNELS + 2];
            feed_buf[i * 2]     = mic1;
            feed_buf[i * 2 + 1] = mic2;
        }

        feed_count++;
        if (1 == feed_count || 0 == (feed_count % 200)) {
            int32_t energy_mic1 = 0, energy_mic2 = 0;
            for (size_t i = 0; i < tdm_frames && i < 64; i++) {
                energy_mic1 += abs(feed_buf[i * 2]);
                energy_mic2 += abs(feed_buf[i * 2 + 1]);
            }
            ESP_LOGI(TAG, "feed #%lu: mic1_e=%ld mic2_e=%ld chunk=%d",
                     (unsigned long)feed_count, (long)energy_mic1, (long)energy_mic2, (int)tdm_frames);
        }

        audio_processor_feed(feed_buf, s_feed_chunk);
        vTaskDelay(1);
    }

    heap_caps_free(tdm_buf);
    heap_caps_free(feed_buf);
    vTaskDelete(NULL);
}

static void fetch_encode_task(void *arg)
{
    (void)arg;
    int16_t *fetch_buf = heap_caps_malloc(s_fetch_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *accum_buf = heap_caps_malloc(OPUS_FRAME_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    uint8_t *opus_buf = heap_caps_malloc(512, MALLOC_CAP_SPIRAM);
    if (NULL == fetch_buf || NULL == accum_buf || NULL == opus_buf) {
        ESP_LOGE(TAG, "fetch_encode_task alloc failed");
        vTaskDelete(NULL);
        return;
    }

    size_t accum_offset = 0;
    uint32_t vad_active_count = 0;

    while (s_running) {
        size_t fetched = 0;
        bool vad = false;
        bool wakeup = false;
        esp_err_t ret = audio_processor_fetch(fetch_buf, &fetched, &vad, &wakeup);
        if (ESP_OK != ret || 0 == fetched) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (wakeup && NULL != s_audio_cfg.on_event) {
            ESP_LOGI(TAG, "WakeNet triggered, notifying app");
            s_audio_cfg.on_event(XIAOZHI_AUDIO_EVENT_WAKE_WORD, s_audio_cfg.user_ctx);
        }

        if (vad && !s_was_speaking) {
            vad_active_count = 0;
        }
        if (vad) {
            vad_active_count++;
        }

        if (!vad && s_was_speaking && NULL != s_audio_cfg.on_event) {
            if (vad_active_count >= MIN_VAD_ACTIVE_FETCHES) {
                ESP_LOGI(TAG, "VAD end detected (active=%lu), notifying app",
                         (unsigned long)vad_active_count);
                s_audio_cfg.on_event(XIAOZHI_AUDIO_EVENT_VAD_END, s_audio_cfg.user_ctx);
            } else {
                ESP_LOGW(TAG, "VAD end ignored (active=%lu < %d)",
                         (unsigned long)vad_active_count, MIN_VAD_ACTIVE_FETCHES);
            }
        }
        s_was_speaking = vad;

        size_t remaining = fetched;
        size_t src_offset = 0;
        while (remaining > 0) {
            size_t to_copy = OPUS_FRAME_SAMPLES - accum_offset;
            if (to_copy > remaining) {
                to_copy = remaining;
            }
            memcpy(&accum_buf[accum_offset], &fetch_buf[src_offset], to_copy * sizeof(int16_t));
            accum_offset += to_copy;
            src_offset += to_copy;
            remaining -= to_copy;

            if (accum_offset >= OPUS_FRAME_SAMPLES) {
                size_t opus_len = 512;
                esp_err_t enc_ret = opus_codec_encode(accum_buf, OPUS_FRAME_SAMPLES, opus_buf, &opus_len);
                if (ESP_OK == enc_ret && opus_len > 0 && NULL != s_audio_cfg.on_opus_recv) {
                    s_audio_cfg.on_opus_recv(opus_buf, (int)opus_len, s_audio_cfg.user_ctx);
                }
                accum_offset = 0;
            }
        }
    }

    heap_caps_free(fetch_buf);
    heap_caps_free(accum_buf);
    heap_caps_free(opus_buf);
    vTaskDelete(NULL);
}

esp_err_t xiaozhi_audio_init(const xiaozhi_audio_config_t *config)
{
    if (NULL == config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_audio_cfg = *config;

    esp_err_t ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        return ret;
    }
    s_out_dev = board_laiwfs300_audio_get_output_dev();

    ret = board_laiwfs300_audio_open_input_all_channels();
    if (ESP_OK != ret) {
        return ret;
    }

    audio_processor_config_t afe_cfg = {
        .mic_channels = 2,
        .ref_channels = 0,
        .enable_ns    = true,
        .enable_aec   = false,
        .enable_vad   = true,
        .enable_wakenet = true,
        .aec_mode     = 0,
    };
    ret = audio_processor_init(&afe_cfg);
    if (ESP_OK != ret) {
        return ret;
    }
    s_feed_chunk = audio_processor_get_feed_chunksize();
    s_fetch_chunk = audio_processor_get_fetch_chunksize();

    opus_encoder_config_t enc_cfg = {
        .sample_rate = OPUS_SAMPLE_RATE,
        .channels = 1,
        .frame_duration_ms = OPUS_FRAME_MS,
        .bitrate = 17000,
        .complexity = 0,
        .enable_vbr = true,
        .enable_dtx = true,
    };
    ret = opus_codec_encoder_init(&enc_cfg);
    if (ESP_OK != ret) {
        return ret;
    }

    opus_decoder_config_t dec_cfg = {
        .sample_rate = OPUS_DEC_SAMPLE_RATE,
        .channels = 1,
        .frame_duration_ms = OPUS_FRAME_MS,
    };
    ret = opus_codec_decoder_init(&dec_cfg);
    if (ESP_OK != ret) {
        return ret;
    }

    amp_enable();
    esp_codec_dev_set_out_vol(s_out_dev, OUTPUT_VOL);

    ret = prompt_player_init();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "prompt_player_init failed: %s (prompt disabled)", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t xiaozhi_audio_start(void)
{
    s_running = true;

    s_playback_queue = xQueueCreate(PLAYBACK_QUEUE_DEPTH, sizeof(opus_frame_item_t));
    if (NULL == s_playback_queue) {
        ESP_LOGE(TAG, "Failed to create playback queue");
        return ESP_ERR_NO_MEM;
    }

    xTaskCreatePinnedToCoreWithCaps(playback_task, "xz_play", PLAYBACK_TASK_STACK,
                                    NULL, PLAYBACK_TASK_PRIO, &s_playback_task,
                                    0, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(feed_task, "xz_feed", 40960, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(fetch_encode_task, "xz_fetch", 40960, NULL, 6, NULL, 0, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Audio pipeline started (feed+fetch+playback tasks)");
    return ESP_OK;
}

esp_err_t xiaozhi_audio_stop(void)
{
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

static void playback_task(void *arg)
{
    (void)arg;
    int16_t *pcm_buf = heap_caps_malloc(OPUS_DEC_FRAME_SAMPLES * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM);
    int16_t *play_buf = heap_caps_malloc(PLAYBACK_FRAME_SAMPLES * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM);
    if (NULL == pcm_buf || NULL == play_buf) {
        ESP_LOGE(TAG, "playback_task: pcm/play buf alloc failed");
        if (NULL != pcm_buf) {
            heap_caps_free(pcm_buf);
        }
        if (NULL != play_buf) {
            heap_caps_free(play_buf);
        }
        vTaskDelete(NULL);
        return;
    }

    uint32_t frame_count = 0;
    ESP_LOGI(TAG, "TTS playback resample: decoder=%d Hz -> codec=%d Hz",
             OPUS_DEC_SAMPLE_RATE, PLAYBACK_SAMPLE_RATE);

    while (s_running) {
        opus_frame_item_t item;
        if (pdTRUE != xQueueReceive(s_playback_queue, &item, pdMS_TO_TICKS(100))) {
            continue;
        }

        if (NULL == item.data) {
            continue;
        }

        if (!s_playing) {
            heap_caps_free(item.data);
            continue;
        }

        frame_count++;
        if (1 == frame_count || 0 == (frame_count % 50)) {
            ESP_LOGI(TAG, "TTS play: frame %lu, opus_len=%d",
                     (unsigned long)frame_count, item.len);
        }

        size_t out_samples = OPUS_DEC_FRAME_SAMPLES;
        esp_err_t dec_ret = opus_codec_decode(item.data, (size_t)item.len,
                                              pcm_buf, &out_samples);
        if (ESP_OK == dec_ret && out_samples > 0) {
            size_t play_samples = resample_tts_to_playback_rate(pcm_buf, out_samples,
                                                                play_buf, PLAYBACK_FRAME_SAMPLES);
            if (play_samples > 0) {
                esp_codec_dev_write(s_out_dev, play_buf, play_samples * sizeof(int16_t));
            }
        }

        heap_caps_free(item.data);
    }

    heap_caps_free(play_buf);
    heap_caps_free(pcm_buf);
    vTaskDelete(NULL);
}

esp_err_t xiaozhi_audio_play_opus(const uint8_t *data, int len)
{
    if (NULL == s_out_dev || NULL == data || 0 >= len || NULL == s_playback_queue) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_playing) {
        if (PROMPT_STATE_PLAYING == s_prompt_state) {
            xiaozhi_audio_prompt_stop();
        }
        esp_codec_dev_set_out_vol(s_out_dev, OUTPUT_VOL);
        s_playing = true;
    }

    uint8_t *copy = heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
    if (NULL == copy) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, data, (size_t)len);

    opus_frame_item_t item = {.data = copy, .len = len};
    if (pdTRUE != xQueueSend(s_playback_queue, &item, 0)) {
        heap_caps_free(copy);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t xiaozhi_audio_play_stop(void)
{
    ESP_LOGI(TAG, "TTS play stopped");
    if (s_playing) {
        s_playing = false;

        opus_frame_item_t item;
        while (pdTRUE == xQueueReceive(s_playback_queue, &item, 0)) {
            if (NULL != item.data) {
                heap_caps_free(item.data);
            }
        }

        audio_processor_reset_buffer();
    }
    return ESP_OK;
}

esp_err_t xiaozhi_audio_play_tone(uint32_t freq_hz, uint32_t duration_ms, float volume)
{
    if (NULL == s_out_dev || 0 == freq_hz || 0 == duration_ms) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t out_rate = SAMPLE_RATE;
    uint32_t total_samples = out_rate * duration_ms / 1000;
    int16_t *buf = heap_caps_malloc(total_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (NULL == buf) {
        audio_processor_disable_aec();
        return ESP_ERR_NO_MEM;
    }

    const float amp = 16000.0f * volume;
    const float step = 2.0f * 3.14159265f * (float)freq_hz / (float)out_rate;
    const uint32_t fade_samples = out_rate * 5 / 1000;

    for (uint32_t i = 0; i < total_samples; i++) {
        float s = amp * sinf(step * (float)i);
        if (i < fade_samples) {
            s *= (float)i / (float)fade_samples;
        } else if (i > total_samples - fade_samples) {
            s *= (float)(total_samples - i) / (float)fade_samples;
        }
        buf[i] = (int16_t)s;
    }

    esp_codec_dev_write(s_out_dev, buf, total_samples * sizeof(int16_t));
    heap_caps_free(buf);

    audio_processor_reset_buffer();
    s_was_speaking = false;

    return ESP_OK;
}

static void prompt_apply_fade_in(int16_t *pcm, int samples, int *remaining)
{
    if (NULL == pcm || NULL == remaining || *remaining <= 0 || samples <= 0) {
        return;
    }

    int fade_samples = (samples < *remaining) ? samples : *remaining;
    int processed = PROMPT_FADE_IN_SAMPLES - *remaining;
    for (int i = 0; i < fade_samples; ++i) {
        int gain = processed + i + 1;
        pcm[i] = (int16_t)(((int32_t)pcm[i] * gain) / PROMPT_FADE_IN_SAMPLES);
    }
    *remaining -= fade_samples;
}

static void prompt_write_silence(void)
{
    if (NULL == s_out_dev) {
        return;
    }
    int16_t silence[PROMPT_PREROLL_SAMPLES] = {0};
    for (int i = 0; i < PROMPT_PREROLL_WRITES; ++i) {
        if (ESP_OK != esp_codec_dev_write(s_out_dev, silence, sizeof(silence))) {
            break;
        }
    }
}

static int prompt_out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    (void)ctx;
    if (NULL == s_out_dev || NULL == data || data_size <= 0) {
        return 0;
    }

    if (s_prompt_fade_in_remaining > 0 && data_size >= (int)sizeof(int16_t)) {
        prompt_apply_fade_in((int16_t *)data,
                             data_size / (int)sizeof(int16_t),
                             &s_prompt_fade_in_remaining);
    }

    (void)esp_codec_dev_write(s_out_dev, data, data_size);
    return 0;
}

static int prompt_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    (void)ctx;
    esp_asp_state_t st = 0;

    if (NULL == event || ESP_ASP_EVENT_TYPE_STATE != event->type) {
        return 0;
    }
    if (NULL == event->payload || sizeof(st) != event->payload_size) {
        return 0;
    }

    memcpy(&st, event->payload, sizeof(st));

    if (ESP_ASP_STATE_STOPPED == st || ESP_ASP_STATE_FINISHED == st || ESP_ASP_STATE_ERROR == st) {
        s_prompt_state = PROMPT_STATE_IDLE;
        s_prompt_fade_in_remaining = 0;
        audio_processor_reset_buffer();
        s_was_speaking = false;
    }

    if (ESP_ASP_STATE_ERROR == st) {
        ESP_LOGE(TAG, "Prompt player error");
    }
    return 0;
}

static esp_err_t prompt_player_init(void)
{
    s_prompt_lock = xSemaphoreCreateMutex();
    if (NULL == s_prompt_lock) {
        return ESP_ERR_NO_MEM;
    }

    esp_asp_cfg_t cfg = {
        .in = { .cb = NULL, .user_ctx = NULL },
        .out = { .cb = prompt_out_data_callback, .user_ctx = NULL },
        .task_prio = 5,
    };

    esp_err_t err = esp_audio_simple_player_new(&cfg, &s_prompt_player);
    if (ESP_OK != err || NULL == s_prompt_player) {
        vSemaphoreDelete(s_prompt_lock);
        s_prompt_lock = NULL;
        return ESP_FAIL;
    }

    err = esp_audio_simple_player_set_event(s_prompt_player, prompt_event_callback, NULL);
    if (ESP_OK != err) {
        esp_audio_simple_player_destroy(s_prompt_player);
        s_prompt_player = NULL;
        vSemaphoreDelete(s_prompt_lock);
        s_prompt_lock = NULL;
        return ESP_FAIL;
    }

    s_prompt_state = PROMPT_STATE_IDLE;
    s_prompt_fade_in_remaining = 0;
    return ESP_OK;
}

esp_err_t xiaozhi_audio_prompt_play(const char *url)
{
    if (NULL == url || NULL == s_prompt_player || NULL == s_prompt_lock) {
        return ESP_ERR_INVALID_ARG;
    }

    if (pdTRUE != xSemaphoreTake(s_prompt_lock, pdMS_TO_TICKS(1000))) {
        return ESP_FAIL;
    }

    if (PROMPT_STATE_CLOSED == s_prompt_state) {
        xSemaphoreGive(s_prompt_lock);
        return ESP_ERR_INVALID_STATE;
    }

    if (PROMPT_STATE_PLAYING == s_prompt_state) {
        esp_audio_simple_player_stop(s_prompt_player);
    }

    esp_codec_dev_set_out_vol(s_out_dev, OUTPUT_VOL);
    prompt_write_silence();

    s_prompt_state = PROMPT_STATE_PLAYING;
    s_prompt_fade_in_remaining = PROMPT_FADE_IN_SAMPLES;
    esp_err_t err = esp_audio_simple_player_run(s_prompt_player, url, NULL);
    if (ESP_OK != err) {
        s_prompt_state = PROMPT_STATE_IDLE;
        s_prompt_fade_in_remaining = 0;
        audio_processor_reset_buffer();
        s_was_speaking = false;
    }

    xSemaphoreGive(s_prompt_lock);
    return err;
}

esp_err_t xiaozhi_audio_prompt_stop(void)
{
    if (NULL == s_prompt_player || NULL == s_prompt_lock) {
        return ESP_ERR_INVALID_ARG;
    }

    if (pdTRUE != xSemaphoreTake(s_prompt_lock, pdMS_TO_TICKS(1000))) {
        return ESP_FAIL;
    }

    if (PROMPT_STATE_PLAYING == s_prompt_state && NULL != s_prompt_player) {
        prompt_write_silence();
        esp_audio_simple_player_stop(s_prompt_player);
    }
    s_prompt_state = PROMPT_STATE_IDLE;
    s_prompt_fade_in_remaining = 0;
    audio_processor_reset_buffer();
    s_was_speaking = false;

    xSemaphoreGive(s_prompt_lock);
    return ESP_OK;
}
