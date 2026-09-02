#ifndef OFDM_AUDIO_H
#define OFDM_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ofdm_profile.h"

#define OFDM_AUDIO_TDM_CHANNEL_COUNT 4U
#define OFDM_AUDIO_READ_FRAMES 256U
#define OFDM_AUDIO_DEFAULT_OUTPUT_VOLUME \
    OFDM_NORMAL_OUTPUT_VOLUME_PERCENT
#define OFDM_AUDIO_DEFAULT_INPUT_GAIN_DB OFDM_NORMAL_INPUT_GAIN_DB

typedef struct {
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t clipped_samples;
    int32_t noise_peak;
    float noise_rms;
} ofdm_audio_diagnostic_t;

esp_err_t ofdm_audio_init(void);
void ofdm_audio_deinit(void);
esp_err_t ofdm_audio_run_diagnostic(ofdm_audio_diagnostic_t *diagnostic);
esp_err_t ofdm_audio_set_amp(bool enabled);
esp_err_t ofdm_audio_set_output_volume(int volume_percent);
esp_err_t ofdm_audio_set_input_gain(float gain_db);
int ofdm_audio_get_output_volume(void);
float ofdm_audio_get_input_gain_db(void);
esp_err_t ofdm_audio_write_mono(const int16_t *samples,
                                size_t sample_count);
esp_err_t ofdm_audio_finish_tx(void);
esp_err_t ofdm_audio_read_mic1(int16_t *samples,
                               size_t frame_count,
                               uint32_t timeout_ms);

#endif
