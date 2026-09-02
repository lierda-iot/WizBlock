#include "ofdm_audio.h"

#include "ofdm_phy.h"

#include "board_pins.h"
#include "bus_i2c.h"
#include "io_expander.h"

#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

#define OFDM_AUDIO_ES8311_ADDRESS 0x30
#define OFDM_AUDIO_ES7210_ADDRESS 0x80
#define OFDM_AUDIO_DMA_DESCRIPTORS 8U
#define OFDM_AUDIO_DMA_FRAMES 256U
#define OFDM_AUDIO_OUTPUT_VOLUME_MIN 0
#define OFDM_AUDIO_OUTPUT_VOLUME_MAX 100
#define OFDM_AUDIO_INPUT_GAIN_DB_MIN 0.0F
#define OFDM_AUDIO_INPUT_GAIN_DB_MAX 37.5F
#define OFDM_AUDIO_WRITE_SAMPLES 1024U
#define OFDM_AUDIO_DRAIN_SAMPLES \
    (OFDM_AUDIO_DMA_DESCRIPTORS * OFDM_AUDIO_DMA_FRAMES)
#define OFDM_AUDIO_DRAIN_DELAY_MS 50U
#define OFDM_AUDIO_DIAG_BLOCKS_PER_POINT 6U
#define OFDM_AUDIO_DIAG_WARMUP_BLOCKS 2U
#define OFDM_AUDIO_DIAG_LEVEL_HZ 3000U
#define OFDM_AUDIO_DIAG_SWEEP_FIRST_HZ 1000U
#define OFDM_AUDIO_DIAG_SWEEP_LAST_HZ 15000U
#define OFDM_AUDIO_DIAG_SWEEP_STEP_HZ 1000U
#define OFDM_AUDIO_DIAG_SWEEP_AMPLITUDE 10000
#define OFDM_AUDIO_DIAG_CLIP_THRESHOLD 32760
#define OFDM_AUDIO_DIAG_TWO_PI 6.28318530717958647692F

static const char *TAG = "ofdm_audio";

static i2s_chan_handle_t s_tx_channel;
static i2s_chan_handle_t s_rx_channel;
static const audio_codec_data_if_t *s_data_interface;
static const audio_codec_ctrl_if_t *s_output_control_interface;
static const audio_codec_if_t *s_output_codec_interface;
static const audio_codec_ctrl_if_t *s_input_control_interface;
static const audio_codec_if_t *s_input_codec_interface;
static const audio_codec_gpio_if_t *s_gpio_interface;
static esp_codec_dev_handle_t s_output_device;
static esp_codec_dev_handle_t s_input_device;
static int16_t s_tdm_buffer[OFDM_AUDIO_READ_FRAMES *
                            OFDM_AUDIO_TDM_CHANNEL_COUNT];
static int16_t s_silence[OFDM_AUDIO_WRITE_SAMPLES];
static int16_t s_diagnostic_output[OFDM_AUDIO_WRITE_SAMPLES];
static int16_t s_diagnostic_input[OFDM_AUDIO_READ_FRAMES];
static bool s_initialized;
static int s_output_volume = OFDM_AUDIO_DEFAULT_OUTPUT_VOLUME;
static float s_input_gain_db = OFDM_AUDIO_DEFAULT_INPUT_GAIN_DB;

typedef struct {
    double square_sum;
    double sine_sum;
    double cosine_sum;
    uint32_t sample_count;
    uint32_t clipped_samples;
    int32_t peak;
} ofdm_audio_measurement_t;

static esp_err_t set_output_volume_internal(int volume_percent);
static esp_err_t set_input_gain_internal(float gain_db);

static void increment_saturated(uint32_t *value)
{
    if (NULL != value && UINT32_MAX > *value) {
        ++(*value);
    }
}

static void accumulate_measurement(ofdm_audio_measurement_t *measurement,
                                   const int16_t *samples,
                                   size_t sample_count,
                                   uint32_t frequency_hz)
{
    if (NULL == measurement || NULL == samples) {
        return;
    }
    const float radians_per_sample =
        OFDM_AUDIO_DIAG_TWO_PI * (float)frequency_hz /
        (float)OFDM_SAMPLE_RATE_HZ;
    for (size_t index = 0U; index < sample_count; ++index) {
        const int16_t sample = samples[index];
        const int32_t magnitude = 0 > sample ? -(int32_t)sample : sample;
        if (measurement->peak < magnitude) {
            measurement->peak = magnitude;
        }
        if (OFDM_AUDIO_DIAG_CLIP_THRESHOLD <= magnitude) {
            increment_saturated(&measurement->clipped_samples);
        }
        measurement->square_sum += (double)sample * sample;
        if (0U < frequency_hz) {
            const float phase = radians_per_sample *
                                (float)measurement->sample_count;
            measurement->sine_sum += (double)sample * sinf(phase);
            measurement->cosine_sum += (double)sample * cosf(phase);
        }
        increment_saturated(&measurement->sample_count);
    }
}

static float measurement_rms(const ofdm_audio_measurement_t *measurement)
{
    if (NULL == measurement || 0U == measurement->sample_count) {
        return 0.0F;
    }
    return (float)sqrt(measurement->square_sum /
                       (double)measurement->sample_count);
}

static float measurement_tone_dbfs(
    const ofdm_audio_measurement_t *measurement)
{
    if (NULL == measurement || 0U == measurement->sample_count) {
        return -120.0F;
    }
    const double tone_peak =
        2.0 * sqrt(measurement->sine_sum * measurement->sine_sum +
                   measurement->cosine_sum * measurement->cosine_sum) /
        (double)measurement->sample_count;
    return 0.0 < tone_peak
               ? (float)(20.0 * log10(tone_peak / 32768.0))
               : -120.0F;
}

static void fill_tone(int16_t *samples, size_t sample_count,
                      uint32_t frequency_hz, int16_t amplitude,
                      float *phase)
{
    if (NULL == samples || NULL == phase) {
        return;
    }
    const float phase_increment =
        OFDM_AUDIO_DIAG_TWO_PI * (float)frequency_hz /
        (float)OFDM_SAMPLE_RATE_HZ;
    float current_phase = *phase;
    for (size_t index = 0U; index < sample_count; ++index) {
        samples[index] = (int16_t)lrintf((float)amplitude *
                                         sinf(current_phase));
        current_phase += phase_increment;
        if (OFDM_AUDIO_DIAG_TWO_PI <= current_phase) {
            current_phase -= OFDM_AUDIO_DIAG_TWO_PI;
        }
    }
    *phase = current_phase;
}

static void drain_receive(void)
{
    while (ESP_OK == ofdm_audio_read_mic1(s_diagnostic_input,
                                           OFDM_AUDIO_READ_FRAMES, 0U)) {
    }
}

static esp_err_t measure_noise(ofdm_audio_diagnostic_t *diagnostic)
{
    ofdm_audio_measurement_t measurement = {0};
    for (uint32_t block = 0U; block < OFDM_AUDIO_DIAG_BLOCKS_PER_POINT;
         ++block) {
        const esp_err_t result = ofdm_audio_read_mic1(
            s_diagnostic_input, OFDM_AUDIO_READ_FRAMES, 100U);
        if (ESP_OK != result) {
            increment_saturated(&diagnostic->rx_errors);
            return result;
        }
        accumulate_measurement(&measurement, s_diagnostic_input,
                               OFDM_AUDIO_READ_FRAMES, 0U);
    }
    diagnostic->noise_peak = measurement.peak;
    diagnostic->noise_rms = measurement_rms(&measurement);
    diagnostic->clipped_samples += measurement.clipped_samples;
    ESP_LOGI(TAG,
             "OFDM_BOOT stage=audio_diag kind=noise rms=%.1f peak=%ld clip=%lu",
             diagnostic->noise_rms, (long)diagnostic->noise_peak,
             (unsigned long)measurement.clipped_samples);
    return ESP_OK;
}

static esp_err_t measure_tone(uint32_t frequency_hz, int16_t amplitude,
                              const char *kind,
                              ofdm_audio_diagnostic_t *diagnostic)
{
    if (NULL == kind || NULL == diagnostic) {
        return ESP_ERR_INVALID_ARG;
    }
    ofdm_audio_measurement_t measurement = {0};
    float phase = 0.0F;
    for (uint32_t block = 0U; block < OFDM_AUDIO_DIAG_BLOCKS_PER_POINT;
         ++block) {
        fill_tone(s_diagnostic_output, OFDM_AUDIO_WRITE_SAMPLES,
                  frequency_hz,
                  amplitude, &phase);
        esp_err_t result = ofdm_audio_write_mono(
            s_diagnostic_output, OFDM_AUDIO_WRITE_SAMPLES);
        if (ESP_OK != result) {
            increment_saturated(&diagnostic->tx_errors);
            return result;
        }
        for (uint32_t read_index = 0U;
             read_index < OFDM_AUDIO_WRITE_SAMPLES /
                              OFDM_AUDIO_READ_FRAMES;
             ++read_index) {
            result = ofdm_audio_read_mic1(s_diagnostic_input,
                                          OFDM_AUDIO_READ_FRAMES, 100U);
            if (ESP_OK != result) {
                increment_saturated(&diagnostic->rx_errors);
                return result;
            }
            if (OFDM_AUDIO_DIAG_WARMUP_BLOCKS <= block) {
                accumulate_measurement(&measurement, s_diagnostic_input,
                                       OFDM_AUDIO_READ_FRAMES,
                                       frequency_hz);
            }
        }
    }
    diagnostic->clipped_samples += measurement.clipped_samples;
    ESP_LOGI(TAG,
             "OFDM_BOOT stage=audio_diag kind=%s hz=%lu tx_amp=%d tone_dbfs=%.1f rms=%.1f peak=%ld clip=%lu",
             kind, (unsigned long)frequency_hz, amplitude,
             measurement_tone_dbfs(&measurement),
             measurement_rms(&measurement), (long)measurement.peak,
             (unsigned long)measurement.clipped_samples);
    return ESP_OK;
}

static esp_err_t initialize_i2s(void)
{
    const i2s_chan_config_t channel_config = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = OFDM_AUDIO_DMA_DESCRIPTORS,
        .dma_frame_num = OFDM_AUDIO_DMA_FRAMES,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    esp_err_t result = i2s_new_channel(&channel_config, &s_tx_channel,
                                       &s_rx_channel);
    if (ESP_OK != result) {
        return result;
    }

    const i2s_std_config_t output_config = {
        .clk_cfg = {
            .sample_rate_hz = OFDM_SAMPLE_RATE_HZ,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_LAIWFS300_I2S_MCLK_GPIO,
            .bclk = BOARD_LAIWFS300_I2S_BCLK_GPIO,
            .ws = BOARD_LAIWFS300_I2S_WS_GPIO,
            .dout = BOARD_LAIWFS300_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    const i2s_tdm_config_t input_config = {
        .clk_cfg = {
            .sample_rate_hz = OFDM_SAMPLE_RATE_HZ,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = (i2s_tdm_slot_mask_t)(
                I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = OFDM_AUDIO_TDM_CHANNEL_COUNT,
        },
        .gpio_cfg = {
            .mclk = BOARD_LAIWFS300_I2S_MCLK_GPIO,
            .bclk = BOARD_LAIWFS300_I2S_BCLK_GPIO,
            .ws = BOARD_LAIWFS300_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = BOARD_LAIWFS300_I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    result = i2s_channel_init_std_mode(s_tx_channel, &output_config);
    if (ESP_OK == result) {
        result = i2s_channel_init_tdm_mode(s_rx_channel, &input_config);
    }
    if (ESP_OK == result) {
        result = i2s_channel_enable(s_tx_channel);
    }
    if (ESP_OK == result) {
        result = i2s_channel_enable(s_rx_channel);
    }
    return result;
}

static esp_err_t initialize_codecs(void)
{
    audio_codec_i2s_cfg_t i2s_config = {
        .port = I2S_NUM_0,
        .rx_handle = s_rx_channel,
        .tx_handle = s_tx_channel,
    };
    s_data_interface = audio_codec_new_i2s_data(&i2s_config);
    s_gpio_interface = audio_codec_new_gpio();
    if (NULL == s_data_interface || NULL == s_gpio_interface) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    if (NULL == bus) {
        return ESP_ERR_INVALID_STATE;
    }
    audio_codec_i2c_cfg_t i2c_config = {
        .port = BOARD_LAIWFS300_I2C_PORT,
        .addr = OFDM_AUDIO_ES8311_ADDRESS,
        .bus_handle = bus,
    };
    s_output_control_interface = audio_codec_new_i2c_ctrl(&i2c_config);
    if (NULL == s_output_control_interface) {
        return ESP_ERR_NO_MEM;
    }
    es8311_codec_cfg_t output_codec_config = {
        .ctrl_if = s_output_control_interface,
        .gpio_if = s_gpio_interface,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .use_mclk = true,
    };
    output_codec_config.hw_gain.pa_voltage = 5.0F;
    output_codec_config.hw_gain.codec_dac_voltage = 3.3F;
    s_output_codec_interface = es8311_codec_new(&output_codec_config);
    if (NULL == s_output_codec_interface) {
        return ESP_FAIL;
    }
    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_output_codec_interface,
        .data_if = s_data_interface,
    };
    s_output_device = esp_codec_dev_new(&device_config);
    if (NULL == s_output_device) {
        return ESP_ERR_NO_MEM;
    }

    i2c_config.addr = OFDM_AUDIO_ES7210_ADDRESS;
    s_input_control_interface = audio_codec_new_i2c_ctrl(&i2c_config);
    if (NULL == s_input_control_interface) {
        return ESP_ERR_NO_MEM;
    }
    es7210_codec_cfg_t input_codec_config = {
        .ctrl_if = s_input_control_interface,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 |
                        ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    s_input_codec_interface = es7210_codec_new(&input_codec_config);
    if (NULL == s_input_codec_interface) {
        return ESP_FAIL;
    }
    device_config.dev_type = ESP_CODEC_DEV_TYPE_IN;
    device_config.codec_if = s_input_codec_interface;
    s_input_device = esp_codec_dev_new(&device_config);
    if (NULL == s_input_device) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_sample_info_t output_format = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = OFDM_SAMPLE_RATE_HZ,
        .mclk_multiple = 0,
    };
    int codec_result = esp_codec_dev_open(s_output_device, &output_format);
    if (ESP_CODEC_DEV_OK != codec_result) {
        return ESP_FAIL;
    }
    if (ESP_OK != set_output_volume_internal(s_output_volume)) {
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t input_format = {
        .bits_per_sample = 16,
        .channel = OFDM_AUDIO_TDM_CHANNEL_COUNT,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1) |
                        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2) |
                        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3),
        .sample_rate = OFDM_SAMPLE_RATE_HZ,
        .mclk_multiple = 0,
    };
    codec_result = esp_codec_dev_open(s_input_device, &input_format);
    if (ESP_CODEC_DEV_OK != codec_result) {
        return ESP_FAIL;
    }
    return ESP_OK == set_input_gain_internal(s_input_gain_db)
               ? ESP_OK
               : ESP_FAIL;
}

static esp_err_t set_output_volume_internal(int volume_percent)
{
    if (NULL == s_output_device ||
        OFDM_AUDIO_OUTPUT_VOLUME_MIN > volume_percent ||
        OFDM_AUDIO_OUTPUT_VOLUME_MAX < volume_percent) {
        return ESP_ERR_INVALID_ARG;
    }
    const int codec_result = esp_codec_dev_set_out_vol(
        s_output_device, volume_percent);
    return ESP_CODEC_DEV_OK == codec_result ? ESP_OK : ESP_FAIL;
}

static esp_err_t set_input_gain_internal(float gain_db)
{
    if (NULL == s_input_device || !isfinite(gain_db) ||
        OFDM_AUDIO_INPUT_GAIN_DB_MIN > gain_db ||
        OFDM_AUDIO_INPUT_GAIN_DB_MAX < gain_db) {
        return ESP_ERR_INVALID_ARG;
    }
    const int codec_result = esp_codec_dev_set_in_channel_gain(
        s_input_device, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), gain_db);
    return ESP_CODEC_DEV_OK == codec_result ? ESP_OK : ESP_FAIL;
}

esp_err_t ofdm_audio_set_amp(bool enabled)
{
    esp_err_t result = io_expander_set_pin_direction(
        BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
        BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    if (ESP_OK == result) {
        result = io_expander_write_pin(
            BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
            BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, enabled);
    }
    return result;
}

esp_err_t ofdm_audio_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t result = ofdm_audio_set_amp(false);
    if (ESP_OK == result) {
        result = initialize_i2s();
    }
    if (ESP_OK == result) {
        result = initialize_codecs();
    }
    if (ESP_OK != result) {
        ofdm_audio_deinit();
        return result;
    }
    s_initialized = true;
    ESP_LOGI(TAG,
             "OFDM_AUDIO rate=%u tx=STD rx=TDM4 mic=slot0 dma=%ux%u vol=%d gain=%.1f",
             (unsigned int)OFDM_SAMPLE_RATE_HZ,
             (unsigned int)OFDM_AUDIO_DMA_DESCRIPTORS,
             (unsigned int)OFDM_AUDIO_DMA_FRAMES,
             s_output_volume, s_input_gain_db);
    return ESP_OK;
}

void ofdm_audio_deinit(void)
{
    (void)ofdm_audio_set_amp(false);
    if (NULL != s_input_device) {
        (void)esp_codec_dev_close(s_input_device);
        esp_codec_dev_delete(s_input_device);
        s_input_device = NULL;
    }
    if (NULL != s_output_device) {
        (void)esp_codec_dev_close(s_output_device);
        esp_codec_dev_delete(s_output_device);
        s_output_device = NULL;
    }
    if (NULL != s_input_codec_interface) {
        audio_codec_delete_codec_if(s_input_codec_interface);
        s_input_codec_interface = NULL;
    }
    if (NULL != s_output_codec_interface) {
        audio_codec_delete_codec_if(s_output_codec_interface);
        s_output_codec_interface = NULL;
    }
    if (NULL != s_input_control_interface) {
        audio_codec_delete_ctrl_if(s_input_control_interface);
        s_input_control_interface = NULL;
    }
    if (NULL != s_output_control_interface) {
        audio_codec_delete_ctrl_if(s_output_control_interface);
        s_output_control_interface = NULL;
    }
    if (NULL != s_gpio_interface) {
        audio_codec_delete_gpio_if(s_gpio_interface);
        s_gpio_interface = NULL;
    }
    if (NULL != s_data_interface) {
        audio_codec_delete_data_if(s_data_interface);
        s_data_interface = NULL;
    }
    if (NULL != s_rx_channel) {
        (void)i2s_channel_disable(s_rx_channel);
        (void)i2s_del_channel(s_rx_channel);
        s_rx_channel = NULL;
    }
    if (NULL != s_tx_channel) {
        (void)i2s_channel_disable(s_tx_channel);
        (void)i2s_del_channel(s_tx_channel);
        s_tx_channel = NULL;
    }
    s_initialized = false;
}

esp_err_t ofdm_audio_set_output_volume(int volume_percent)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = set_output_volume_internal(volume_percent);
    if (ESP_OK == result) {
        s_output_volume = volume_percent;
    }
    return result;
}

esp_err_t ofdm_audio_set_input_gain(float gain_db)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = set_input_gain_internal(gain_db);
    if (ESP_OK == result) {
        s_input_gain_db = gain_db;
    }
    return result;
}

int ofdm_audio_get_output_volume(void)
{
    return s_output_volume;
}

float ofdm_audio_get_input_gain_db(void)
{
    return s_input_gain_db;
}

esp_err_t ofdm_audio_run_diagnostic(ofdm_audio_diagnostic_t *diagnostic)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == diagnostic) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(diagnostic, 0, sizeof(*diagnostic));
    esp_err_t result = ofdm_audio_set_amp(false);
    if (ESP_OK != result) {
        return result;
    }
    drain_receive();
    result = measure_noise(diagnostic);
    if (ESP_OK == result) {
        result = ofdm_audio_set_amp(true);
    }
    static const int16_t level_amplitudes[] = {4096, 8192, 16384};
    for (size_t index = 0U;
         ESP_OK == result &&
         index < sizeof(level_amplitudes) / sizeof(level_amplitudes[0]);
         ++index) {
        result = measure_tone(OFDM_AUDIO_DIAG_LEVEL_HZ,
                              level_amplitudes[index], "level",
                              diagnostic);
    }
    for (uint32_t frequency_hz = OFDM_AUDIO_DIAG_SWEEP_FIRST_HZ;
         ESP_OK == result &&
         frequency_hz <= OFDM_AUDIO_DIAG_SWEEP_LAST_HZ;
         frequency_hz += OFDM_AUDIO_DIAG_SWEEP_STEP_HZ) {
        result = measure_tone(frequency_hz,
                              OFDM_AUDIO_DIAG_SWEEP_AMPLITUDE, "sweep",
                              diagnostic);
    }
    const esp_err_t finish_result = ofdm_audio_finish_tx();
    const esp_err_t amp_result = ofdm_audio_set_amp(false);
    drain_receive();
    if (ESP_OK == result && ESP_OK != finish_result) {
        increment_saturated(&diagnostic->tx_errors);
        result = finish_result;
    }
    if (ESP_OK == result && ESP_OK != amp_result) {
        result = amp_result;
    }
    ESP_LOGI(TAG,
             "OFDM_BOOT stage=audio_diag kind=complete result=%s rx_errors=%lu tx_errors=%lu clip=%lu",
             ESP_OK == result ? "OK" : "FAIL",
             (unsigned long)diagnostic->rx_errors,
             (unsigned long)diagnostic->tx_errors,
             (unsigned long)diagnostic->clipped_samples);
    return result;
}

esp_err_t ofdm_audio_write_mono(const int16_t *samples,
                                size_t sample_count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == samples || 0U == sample_count) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t offset = 0U;
    while (offset < sample_count) {
        size_t chunk = sample_count - offset;
        if (OFDM_AUDIO_WRITE_SAMPLES < chunk) {
            chunk = OFDM_AUDIO_WRITE_SAMPLES;
        }
        int result = esp_codec_dev_write(
            s_output_device, (void *)&samples[offset],
            chunk * sizeof(samples[0]));
        if (ESP_CODEC_DEV_OK != result) {
            return ESP_FAIL;
        }
        offset += chunk;
    }
    return ESP_OK;
}

esp_err_t ofdm_audio_finish_tx(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t remaining = OFDM_AUDIO_DRAIN_SAMPLES;
    while (0U < remaining) {
        size_t chunk = remaining;
        if (OFDM_AUDIO_WRITE_SAMPLES < chunk) {
            chunk = OFDM_AUDIO_WRITE_SAMPLES;
        }
        esp_err_t result = ofdm_audio_write_mono(s_silence, chunk);
        if (ESP_OK != result) {
            return result;
        }
        remaining -= chunk;
    }
    vTaskDelay(pdMS_TO_TICKS(OFDM_AUDIO_DRAIN_DELAY_MS));
    return ESP_OK;
}

esp_err_t ofdm_audio_read_mic1(int16_t *samples,
                               size_t frame_count,
                               uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == samples || 0U == frame_count ||
        OFDM_AUDIO_READ_FRAMES < frame_count) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t bytes_requested = frame_count *
                                   OFDM_AUDIO_TDM_CHANNEL_COUNT *
                                   sizeof(s_tdm_buffer[0]);
    size_t bytes_read = 0U;
    esp_err_t result = i2s_channel_read(s_rx_channel, s_tdm_buffer,
                                        bytes_requested, &bytes_read,
                                        timeout_ms);
    if (ESP_OK != result) {
        return result;
    }
    if (bytes_requested != bytes_read) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t index = 0U; index < frame_count; ++index) {
        samples[index] = s_tdm_buffer[index * OFDM_AUDIO_TDM_CHANNEL_COUNT];
    }
    return ESP_OK;
}
