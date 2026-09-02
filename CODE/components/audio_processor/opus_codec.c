#include "opus_codec.h"

#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include "esp_check.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "opus_codec";

static void *s_encoder;
static void *s_decoder;
static int s_encoder_frame_size;
static int s_encoder_outbuf_size;
static int s_decoder_frame_size;

#define OPUS_GET_FRAME_DUR_ENUM(ms) \
    ((ms) == 10 ? ESP_OPUS_ENC_FRAME_DURATION_10_MS : \
     (ms) == 20 ? ESP_OPUS_ENC_FRAME_DURATION_20_MS : \
     (ms) == 40 ? ESP_OPUS_ENC_FRAME_DURATION_40_MS : \
     (ms) == 60 ? ESP_OPUS_ENC_FRAME_DURATION_60_MS : \
     ESP_OPUS_ENC_FRAME_DURATION_60_MS)

#define OPUS_GET_DEC_FRAME_DUR_ENUM(ms) \
    ((ms) == 10 ? ESP_OPUS_DEC_FRAME_DURATION_10_MS : \
     (ms) == 20 ? ESP_OPUS_DEC_FRAME_DURATION_20_MS : \
     (ms) == 40 ? ESP_OPUS_DEC_FRAME_DURATION_40_MS : \
     (ms) == 60 ? ESP_OPUS_DEC_FRAME_DURATION_60_MS : \
     ESP_OPUS_DEC_FRAME_DURATION_60_MS)

esp_err_t opus_codec_encoder_init(const opus_encoder_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(NULL != cfg, ESP_ERR_INVALID_ARG, TAG, "cfg null");
    ESP_RETURN_ON_FALSE(NULL == s_encoder, ESP_ERR_INVALID_STATE, TAG, "encoder exists");

    esp_opus_enc_config_t enc_cfg = {
        .sample_rate = (int)cfg->sample_rate,
        .channel = (int)cfg->channels,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = (0 != cfg->bitrate) ? cfg->bitrate : ESP_OPUS_BITRATE_AUTO,
        .frame_duration = OPUS_GET_FRAME_DUR_ENUM(cfg->frame_duration_ms),
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = cfg->complexity,
        .enable_fec = false,
        .enable_dtx = cfg->enable_dtx,
        .enable_vbr = cfg->enable_vbr,
    };

    int ret = esp_opus_enc_open(&enc_cfg, sizeof(enc_cfg), &s_encoder);
    ESP_RETURN_ON_FALSE(NULL != s_encoder, ESP_FAIL, TAG, "enc open fail: %d", ret);

    esp_opus_enc_get_frame_size(s_encoder, &s_encoder_frame_size, &s_encoder_outbuf_size);
    s_encoder_frame_size /= (int)sizeof(int16_t);

    ESP_LOGI(TAG, "Opus encoder: rate=%lu ch=%u frame=%d samples outbuf=%d bytes",
             (unsigned long)cfg->sample_rate, cfg->channels,
             s_encoder_frame_size, s_encoder_outbuf_size);
    return ESP_OK;
}

size_t opus_codec_encoder_frame_samples(void)
{
    return (size_t)s_encoder_frame_size;
}

esp_err_t opus_codec_encode(const int16_t *pcm, size_t samples, uint8_t *out, size_t *out_len)
{
    ESP_RETURN_ON_FALSE(NULL != s_encoder, ESP_ERR_INVALID_STATE, TAG, "no encoder");
    ESP_RETURN_ON_FALSE(NULL != pcm && NULL != out && NULL != out_len, ESP_ERR_INVALID_ARG, TAG, "arg null");

    esp_audio_enc_in_frame_t in = {
        .buffer = (uint8_t *)pcm,
        .len = (uint32_t)(samples * sizeof(int16_t)),
    };
    esp_audio_enc_out_frame_t out_frame = {
        .buffer = out,
        .len = (uint32_t)*out_len,
        .encoded_bytes = 0,
    };

    int ret = esp_opus_enc_process(s_encoder, &in, &out_frame);
    if (ESP_AUDIO_ERR_OK != ret) {
        ESP_LOGE(TAG, "encode fail: %d", ret);
        return ESP_FAIL;
    }
    *out_len = out_frame.encoded_bytes;
    return ESP_OK;
}

esp_err_t opus_codec_decoder_init(const opus_decoder_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(NULL != cfg, ESP_ERR_INVALID_ARG, TAG, "cfg null");
    ESP_RETURN_ON_FALSE(NULL == s_decoder, ESP_ERR_INVALID_STATE, TAG, "decoder exists");

    esp_opus_dec_cfg_t dec_cfg = {
        .sample_rate = cfg->sample_rate,
        .channel = (uint8_t)cfg->channels,
        .frame_duration = OPUS_GET_DEC_FRAME_DUR_ENUM(cfg->frame_duration_ms),
        .self_delimited = false,
    };

    int ret = esp_opus_dec_open(&dec_cfg, sizeof(dec_cfg), &s_decoder);
    ESP_RETURN_ON_FALSE(NULL != s_decoder, ESP_FAIL, TAG, "dec open fail: %d", ret);

    s_decoder_frame_size = (int)(cfg->sample_rate * cfg->frame_duration_ms / 1000);

    ESP_LOGI(TAG, "Opus decoder: rate=%lu ch=%u frame=%d samples",
             (unsigned long)cfg->sample_rate, cfg->channels, s_decoder_frame_size);
    return ESP_OK;
}

size_t opus_codec_decoder_frame_samples(void)
{
    return (size_t)s_decoder_frame_size;
}

esp_err_t opus_codec_decode(const uint8_t *data, size_t len, int16_t *out, size_t *out_samples)
{
    ESP_RETURN_ON_FALSE(NULL != s_decoder, ESP_ERR_INVALID_STATE, TAG, "no decoder");
    ESP_RETURN_ON_FALSE(NULL != data && NULL != out && NULL != out_samples, ESP_ERR_INVALID_ARG, TAG, "arg null");

    esp_audio_dec_in_raw_t raw = {
        .buffer = (uint8_t *)data,
        .len = (uint32_t)len,
        .consumed = 0,
        .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
    };
    esp_audio_dec_out_frame_t out_frame = {
        .buffer = (uint8_t *)out,
        .len = (uint32_t)(*out_samples * sizeof(int16_t)),
        .decoded_size = 0,
    };
    esp_audio_dec_info_t dec_info = {0};

    int ret = esp_opus_dec_decode(s_decoder, &raw, &out_frame, &dec_info);
    if (ESP_AUDIO_ERR_OK != ret) {
        ESP_LOGE(TAG, "decode fail: %d", ret);
        return ESP_FAIL;
    }
    *out_samples = out_frame.decoded_size / sizeof(int16_t);
    return ESP_OK;
}

void opus_codec_deinit(void)
{
    if (NULL != s_encoder) {
        esp_opus_enc_close(s_encoder);
        s_encoder = NULL;
    }
    if (NULL != s_decoder) {
        esp_opus_dec_close(s_decoder);
        s_decoder = NULL;
    }
    s_encoder_frame_size = 0;
    s_encoder_outbuf_size = 0;
    s_decoder_frame_size = 0;
    ESP_LOGI(TAG, "Opus codec deinitialized");
}
