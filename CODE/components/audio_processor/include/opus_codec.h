#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t frame_duration_ms;
    int32_t bitrate;
    uint8_t complexity;
    bool enable_vbr;
    bool enable_dtx;
} opus_encoder_config_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t frame_duration_ms;
} opus_decoder_config_t;

esp_err_t opus_codec_encoder_init(const opus_encoder_config_t *cfg);
size_t opus_codec_encoder_frame_samples(void);
esp_err_t opus_codec_encode(const int16_t *pcm, size_t samples, uint8_t *out, size_t *out_len);
esp_err_t opus_codec_decoder_init(const opus_decoder_config_t *cfg);
size_t opus_codec_decoder_frame_samples(void);
esp_err_t opus_codec_decode(const uint8_t *data, size_t len, int16_t *out, size_t *out_samples);
void opus_codec_deinit(void);
