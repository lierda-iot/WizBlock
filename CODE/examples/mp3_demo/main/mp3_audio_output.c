#include "mp3_audio_output.h"

#include "board_pins.h"
#include "bus_i2c.h"
#include "io_expander.h"

#include "driver/i2s_std.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_render.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_extractor_defaults.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_log.h"
#include "media_lib_adapter.h"

#include <stddef.h>
#include <stdint.h>

static const char *TAG = "mp3_audio";

#define MP3_AUDIO_ES8311_ADDRESS 0x30
#define MP3_AUDIO_SAMPLE_RATE 44100U
#define MP3_AUDIO_BITS 16U
#define MP3_AUDIO_CHANNELS 1U
#define MP3_AUDIO_VOLUME 80

static i2s_chan_handle_t s_tx_channel;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_if_t *s_codec_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static esp_codec_dev_handle_t s_codec_dev;
static esp_gmf_pool_handle_t s_pool;
static esp_audio_render_handle_t s_render;
static bool s_media_registered;
static bool s_initialized;

static int render_writer(uint8_t *pcm, uint32_t length, void *context)
{
    esp_codec_dev_handle_t codec = context;

    if (NULL == codec || NULL == pcm || 0U == length) {
        return -1;
    }
    return esp_codec_dev_write(codec, pcm, length);
}

static esp_err_t initialize_i2s(void)
{
    const i2s_chan_config_t channel_config = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 256,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    const i2s_std_config_t standard_config = {
        .clk_cfg = {
            .sample_rate_hz = MP3_AUDIO_SAMPLE_RATE,
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

    esp_err_t result = i2s_new_channel(&channel_config, &s_tx_channel, NULL);
    if (ESP_OK == result) {
        result = i2s_channel_init_std_mode(s_tx_channel, &standard_config);
    }
    if (ESP_OK == result) {
        result = i2s_channel_enable(s_tx_channel);
    }
    return result;
}

static esp_err_t initialize_codec(void)
{
    audio_codec_i2s_cfg_t i2s_config = {
        .port = I2S_NUM_0,
        .rx_handle = NULL,
        .tx_handle = s_tx_channel,
    };
    audio_codec_i2c_cfg_t i2c_config = {
        .port = 0,
        .addr = MP3_AUDIO_ES8311_ADDRESS,
        .bus_handle = bus_i2c_master_bus(),
    };

    if (NULL == i2c_config.bus_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    s_data_if = audio_codec_new_i2s_data(&i2s_config);
    s_gpio_if = audio_codec_new_gpio();
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    if (NULL == s_data_if || NULL == s_gpio_if || NULL == s_ctrl_if) {
        return ESP_ERR_NO_MEM;
    }

    es8311_codec_cfg_t codec_config = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .use_mclk = true,
    };
    codec_config.hw_gain.pa_voltage = 5.0;
    codec_config.hw_gain.codec_dac_voltage = 3.3;
    s_codec_if = es8311_codec_new(&codec_config);
    if (NULL == s_codec_if) {
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    s_codec_dev = esp_codec_dev_new(&device_config);
    if (NULL == s_codec_dev) {
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = MP3_AUDIO_BITS,
        .channel = MP3_AUDIO_CHANNELS,
        .channel_mask = 0,
        .sample_rate = MP3_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    if (ESP_CODEC_DEV_OK != esp_codec_dev_open(s_codec_dev, &sample_info) ||
        ESP_CODEC_DEV_OK !=
            esp_codec_dev_set_out_vol(s_codec_dev, MP3_AUDIO_VOLUME)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t initialize_render(
    esp_audio_render_stream_handle_t *render_stream)
{
    esp_gmf_element_handle_t element = NULL;

    if (ESP_GMF_ERR_OK != esp_gmf_pool_init(&s_pool)) {
        return ESP_FAIL;
    }
    esp_ae_ch_cvt_cfg_t channel_config = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    if (ESP_GMF_ERR_OK != esp_gmf_ch_cvt_init(&channel_config, &element) ||
        ESP_GMF_ERR_OK != esp_gmf_pool_register_element(s_pool, element, NULL)) {
        return ESP_FAIL;
    }
    esp_ae_bit_cvt_cfg_t bit_config = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    if (ESP_GMF_ERR_OK != esp_gmf_bit_cvt_init(&bit_config, &element) ||
        ESP_GMF_ERR_OK != esp_gmf_pool_register_element(s_pool, element, NULL)) {
        return ESP_FAIL;
    }
    esp_ae_rate_cvt_cfg_t rate_config = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    if (ESP_GMF_ERR_OK != esp_gmf_rate_cvt_init(&rate_config, &element) ||
        ESP_GMF_ERR_OK != esp_gmf_pool_register_element(s_pool, element, NULL)) {
        return ESP_FAIL;
    }

    esp_audio_render_cfg_t render_config = {
        .max_stream_num = 1,
        .out_writer = render_writer,
        .out_ctx = s_codec_dev,
        .out_sample_info = {
            .sample_rate = MP3_AUDIO_SAMPLE_RATE,
            .bits_per_sample = MP3_AUDIO_BITS,
            .channel = MP3_AUDIO_CHANNELS,
        },
        .pool = s_pool,
        .process_period = 20,
    };
    if (ESP_AUDIO_RENDER_ERR_OK !=
        esp_audio_render_create(&render_config, &s_render)) {
        return ESP_FAIL;
    }
    if (ESP_AUDIO_RENDER_ERR_OK != esp_audio_render_stream_get(
            s_render, ESP_AUDIO_RENDER_STREAM_ID(0), render_stream)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mp3_audio_output_set_amp(bool enabled)
{
    esp_err_t result = io_expander_set_pin_direction(
        BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
        BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    if (ESP_OK == result) {
        result = io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                       BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN,
                                       enabled);
    }
    return result;
}

esp_err_t mp3_audio_output_init(
    esp_audio_render_stream_handle_t *render_stream)
{
    esp_err_t result = ESP_OK;

    if (NULL == render_stream) {
        return ESP_ERR_INVALID_ARG;
    }
    *render_stream = NULL;
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    media_lib_add_default_adapter();
    esp_extractor_register_default();
    esp_audio_dec_register_default();
    s_media_registered = true;

    result = mp3_audio_output_set_amp(false);
    if (ESP_OK == result) {
        result = initialize_i2s();
    }
    if (ESP_OK == result) {
        result = initialize_codec();
    }
    if (ESP_OK == result) {
        result = initialize_render(render_stream);
    }
    if (ESP_OK != result) {
        mp3_audio_output_deinit();
        return result;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "INIT ok rate=%u bits=%u channels=%u volume=%d",
             (unsigned int)MP3_AUDIO_SAMPLE_RATE,
             (unsigned int)MP3_AUDIO_BITS,
             (unsigned int)MP3_AUDIO_CHANNELS, MP3_AUDIO_VOLUME);
    return ESP_OK;
}

void mp3_audio_output_deinit(void)
{
    (void)mp3_audio_output_set_amp(false);
    if (NULL != s_render) {
        esp_audio_render_destroy(s_render);
        s_render = NULL;
    }
    if (NULL != s_pool) {
        esp_gmf_pool_deinit(s_pool);
        s_pool = NULL;
    }
    if (NULL != s_codec_dev) {
        esp_codec_dev_delete(s_codec_dev);
        s_codec_dev = NULL;
    }
    if (NULL != s_codec_if) {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = NULL;
    }
    if (NULL != s_ctrl_if) {
        audio_codec_delete_ctrl_if(s_ctrl_if);
        s_ctrl_if = NULL;
    }
    if (NULL != s_gpio_if) {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = NULL;
    }
    if (NULL != s_data_if) {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }
    if (NULL != s_tx_channel) {
        (void)i2s_channel_disable(s_tx_channel);
        (void)i2s_del_channel(s_tx_channel);
        s_tx_channel = NULL;
    }
    if (s_media_registered) {
        esp_audio_dec_unregister_default();
        esp_extractor_unregister_default();
        s_media_registered = false;
    }
    s_initialized = false;
}
