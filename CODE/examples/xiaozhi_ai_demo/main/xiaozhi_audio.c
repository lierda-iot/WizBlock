#include "xiaozhi_audio.h"
#include "board_laiwfs300.h"
#include "audio_processor.h"
#include "opus_codec.h"
#include "board_pins.h"
#include "bus_i2c.h"
#include "io_expander.h"

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
#define OUTPUT_VOL        50

static xiaozhi_audio_config_t s_audio_cfg;
static esp_codec_dev_handle_t s_out_dev;
static size_t s_feed_chunk;
static size_t s_fetch_chunk;
static volatile bool s_running;
static volatile bool s_playing;
static volatile bool s_was_speaking;

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
    int16_t *feed_buf = heap_caps_malloc(s_feed_chunk * 3 * sizeof(int16_t),
                                          MALLOC_CAP_SPIRAM);
    if (NULL == tdm_buf || NULL == feed_buf) {
        ESP_LOGE(TAG, "feed_task alloc failed");
        vTaskDelete(NULL);
        return;
    }

    while (s_running) {
        esp_err_t ret = board_laiwfs300_audio_read_tdm_4ch(tdm_buf, tdm_frames);
        if (ESP_OK != ret) {
            vTaskDelay(1);
            continue;
        }

        for (size_t i = 0; i < tdm_frames; i++) {
            feed_buf[i * 3]     = tdm_buf[i * TDM_CHANNELS + 0];
            feed_buf[i * 3 + 1] = tdm_buf[i * TDM_CHANNELS + 2];
            feed_buf[i * 3 + 2] = tdm_buf[i * TDM_CHANNELS + 1];
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
            s_audio_cfg.on_event(XIAOZHI_AUDIO_EVENT_WAKE_WORD, s_audio_cfg.user_ctx);
        }

        if (!vad && s_was_speaking && NULL != s_audio_cfg.on_event) {
            s_audio_cfg.on_event(XIAOZHI_AUDIO_EVENT_VAD_END, s_audio_cfg.user_ctx);
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
        .ref_channels = 1,
        .enable_ns    = true,
        .enable_aec   = true,
        .enable_vad   = true,
        .enable_wakenet = true,
        .aec_mode     = 3,
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
        .bitrate = 27800,
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

    return ESP_OK;
}

esp_err_t xiaozhi_audio_start(void)
{
    s_running = true;
    xTaskCreatePinnedToCoreWithCaps(feed_task, "xz_feed", 40960, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(fetch_encode_task, "xz_fetch", 40960, NULL, 6, NULL, 0, MALLOC_CAP_SPIRAM);
    return ESP_OK;
}

esp_err_t xiaozhi_audio_stop(void)
{
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

esp_err_t xiaozhi_audio_play_opus(const uint8_t *data, int len)
{
    if (NULL == s_out_dev || NULL == data || 0 >= len) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t *pcm_buf = heap_caps_malloc(OPUS_DEC_FRAME_SAMPLES * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM);
    if (NULL == pcm_buf) {
        return ESP_ERR_NO_MEM;
    }

    size_t out_samples = OPUS_DEC_FRAME_SAMPLES;
    esp_err_t dec_ret = opus_codec_decode(data, (size_t)len, pcm_buf, &out_samples);
    if (ESP_OK == dec_ret && out_samples > 0) {
        s_playing = true;
        esp_codec_dev_write(s_out_dev, pcm_buf, out_samples * sizeof(int16_t));
    }

    heap_caps_free(pcm_buf);
    return ESP_OK;
}

esp_err_t xiaozhi_audio_play_stop(void)
{
    s_playing = false;
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
    return ESP_OK;
}
