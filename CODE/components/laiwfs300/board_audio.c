#include "board_laiwfs300.h"

#include "board_pins.h"
#include "bus_i2c.h"
#include "io_expander.h"

#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

static const char *TAG = "board_audio";

#define ES8311_ADDR 0x30
#define ES7210_ADDR 0x80

static i2s_chan_handle_t s_tx_chan;
static i2s_chan_handle_t s_rx_chan;

static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_out_ctrl_if;
static const audio_codec_if_t *s_out_codec_if;
static const audio_codec_ctrl_if_t *s_in_ctrl_if;
static const audio_codec_if_t *s_in_codec_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static esp_codec_dev_handle_t s_output_dev;
static esp_codec_dev_handle_t s_input_dev;

static bool s_initialized;

static esp_err_t create_i2s_channels(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan), TAG, "new channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)BOARD_LAIWFS300_I2S_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_LAIWFS300_I2S_MCLK_GPIO,
            .bclk = BOARD_LAIWFS300_I2S_BCLK_GPIO,
            .ws   = BOARD_LAIWFS300_I2S_WS_GPIO,
            .dout = BOARD_LAIWFS300_I2S_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)BOARD_LAIWFS300_I2S_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = BOARD_LAIWFS300_I2S_MCLK_GPIO,
            .bclk = BOARD_LAIWFS300_I2S_BCLK_GPIO,
            .ws   = BOARD_LAIWFS300_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = BOARD_LAIWFS300_I2S_DIN_GPIO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "init tx std");
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(s_rx_chan, &tdm_cfg), TAG, "init rx tdm");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "enable tx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "enable rx");

    ESP_LOGI(TAG, "I2S0 duplex: TX=STD RX=TDM MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d rate=%d",
             BOARD_LAIWFS300_I2S_MCLK_GPIO, BOARD_LAIWFS300_I2S_BCLK_GPIO,
             BOARD_LAIWFS300_I2S_WS_GPIO, BOARD_LAIWFS300_I2S_DOUT_GPIO,
             BOARD_LAIWFS300_I2S_DIN_GPIO, BOARD_LAIWFS300_I2S_SAMPLE_RATE);
    return ESP_OK;
}

static esp_err_t create_codec_devices(void)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = s_rx_chan,
        .tx_handle = s_tx_chan,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(NULL != s_data_if, ESP_FAIL, TAG, "new i2s data");

    s_gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(NULL != s_gpio_if, ESP_FAIL, TAG, "new gpio");

    i2c_master_bus_handle_t i2c_bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != i2c_bus, ESP_ERR_INVALID_STATE, TAG, "i2c bus null");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = ES8311_ADDR,
        .bus_handle = i2c_bus,
    };
    s_out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(NULL != s_out_ctrl_if, ESP_FAIL, TAG, "new i2c ctrl es8311");

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = s_out_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .use_mclk = true,
    };
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    s_out_codec_if = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(NULL != s_out_codec_if, ESP_FAIL, TAG, "es8311_codec_new");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_out_codec_if,
        .data_if = s_data_if,
    };
    s_output_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(NULL != s_output_dev, ESP_FAIL, TAG, "new output dev");

    i2c_cfg.addr = ES7210_ADDR;
    s_in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(NULL != s_in_ctrl_if, ESP_FAIL, TAG, "new i2c ctrl es7210");

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = s_in_ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    s_in_codec_if = es7210_codec_new(&es7210_cfg);
    ESP_RETURN_ON_FALSE(NULL != s_in_codec_if, ESP_FAIL, TAG, "es7210_codec_new");

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = s_in_codec_if;
    s_input_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(NULL != s_input_dev, ESP_FAIL, TAG, "new input dev");

    ESP_LOGI(TAG, "codec devices created (ES8311 out, ES7210 in)");
    return ESP_OK;
}

esp_err_t board_laiwfs300_audio_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(create_i2s_channels(), TAG, "i2s");
    ESP_RETURN_ON_ERROR(create_codec_devices(), TAG, "codec dev");

    esp_codec_dev_sample_info_t out_fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = BOARD_LAIWFS300_I2S_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_output_dev, &out_fs), TAG, "open output");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_output_dev, 80), TAG, "set out vol");

    esp_codec_dev_sample_info_t in_fs = {
        .bits_per_sample = 16,
        .channel = 4,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)
                      | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3),
        .sample_rate = BOARD_LAIWFS300_I2S_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_input_dev, &in_fs), TAG, "open input");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_channel_gain(s_input_dev,
        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1), 30.0),
        TAG, "set in gain");

    s_initialized = true;
    ESP_LOGI(TAG, "audio initialized (esp_codec_dev)");
    return ESP_OK;
}

esp_err_t board_laiwfs300_audio_play_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);

    const uint32_t sample_rate = BOARD_LAIWFS300_I2S_SAMPLE_RATE;
    const uint32_t total_samples = (sample_rate * duration_ms) / 1000;
    const size_t buf_frames = 128;
    int16_t buf[128];

    ESP_LOGI(TAG, "playing %lu Hz tone for %lu ms", (unsigned long)freq_hz, (unsigned long)duration_ms);

    uint32_t written_samples = 0;
    while (written_samples < total_samples) {
        size_t chunk = (total_samples - written_samples < buf_frames)
                       ? (total_samples - written_samples) : buf_frames;
        for (size_t i = 0; i < chunk; i++) {
            float t = (float)(written_samples + i) / (float)sample_rate;
            buf[i] = (int16_t)(16000.0f * sinf(2.0f * 3.14159265f * (float)freq_hz * t));
        }
        esp_codec_dev_write(s_output_dev, buf, chunk * sizeof(int16_t));
        written_samples += chunk;
    }

    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, false);
    ESP_LOGI(TAG, "tone done");
    return ESP_OK;
}

esp_err_t board_laiwfs300_audio_mic_test(uint32_t duration_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t sample_rate = BOARD_LAIWFS300_I2S_SAMPLE_RATE;
    const uint32_t total_frames = (sample_rate * duration_ms) / 1000;
    int16_t buf[512];
    uint32_t read_frames = 0;
    int64_t sum_sq_l = 0;
    int64_t sum_sq_r = 0;

    while (read_frames < total_frames) {
        int ret = esp_codec_dev_read(s_input_dev, buf, sizeof(buf));
        if (ESP_OK != ret) {
            break;
        }
        size_t frames = sizeof(buf) / (2 * sizeof(int16_t));
        for (size_t i = 0; i < frames; i++) {
            int16_t left = buf[i * 2];
            int16_t right = buf[i * 2 + 1];
            sum_sq_l += (int64_t)left * left;
            sum_sq_r += (int64_t)right * right;
        }
        read_frames += frames;
    }

    uint32_t rms_l = (uint32_t)sqrtf((float)sum_sq_l / (float)(read_frames > 0 ? read_frames : 1));
    uint32_t rms_r = (uint32_t)sqrtf((float)sum_sq_r / (float)(read_frames > 0 ? read_frames : 1));
    ESP_LOGI(TAG, "mic test: %lu frames, MIC1(L) RMS=%lu, MIC2(R) RMS=%lu",
             (unsigned long)read_frames, (unsigned long)rms_l, (unsigned long)rms_r);
    return ESP_OK;
}

static void play_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    const uint32_t sample_rate = BOARD_LAIWFS300_I2S_SAMPLE_RATE;
    const uint32_t total_samples = (sample_rate * duration_ms) / 1000;
    const size_t buf_frames = 128;
    int16_t buf[128];
    uint32_t written = 0;

    while (written < total_samples) {
        size_t chunk = (total_samples - written < buf_frames)
                       ? (total_samples - written) : buf_frames;
        for (size_t i = 0; i < chunk; i++) {
            float t = (float)(written + i) / (float)sample_rate;
            buf[i] = (int16_t)(12000.0f * sinf(2.0f * 3.14159265f * (float)freq_hz * t));
        }
        esp_codec_dev_write(s_output_dev, buf, chunk * sizeof(int16_t));
        written += chunk;
    }
}

static void audio_record_play_task(void *arg)
{
    (void)arg;
    const uint32_t sample_rate = BOARD_LAIWFS300_I2S_SAMPLE_RATE;
    const uint32_t record_seconds = 10;
    const uint32_t total_bytes = sample_rate * record_seconds * sizeof(int16_t);

    int16_t *rec_buf = heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM);
    if (NULL == rec_buf) {
        ESP_LOGE(TAG, "record-play: PSRAM alloc failed (%lu bytes)", (unsigned long)total_bytes);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "record-play: beep -> record %lus -> beep -> play %lus",
             (unsigned long)record_seconds, (unsigned long)record_seconds);

    play_beep(1000, 200);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "record-play: RECORDING...");
    uint32_t recorded_bytes = 0;
    while (recorded_bytes < total_bytes) {
        size_t chunk = 512 * sizeof(int16_t);
        if (recorded_bytes + chunk > total_bytes) {
            chunk = total_bytes - recorded_bytes;
        }
        int ret = esp_codec_dev_read(s_input_dev, (uint8_t *)rec_buf + recorded_bytes, chunk);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "record-play: read error at %lu bytes", (unsigned long)recorded_bytes);
            break;
        }
        recorded_bytes += chunk;
    }

    ESP_LOGI(TAG, "record-play: recorded %lu bytes", (unsigned long)recorded_bytes);
    play_beep(1500, 200);
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_LOGI(TAG, "record-play: PLAYING...");
    uint32_t played_bytes = 0;
    while (played_bytes < recorded_bytes) {
        size_t chunk = 512 * sizeof(int16_t);
        if (played_bytes + chunk > recorded_bytes) {
            chunk = recorded_bytes - played_bytes;
        }
        esp_codec_dev_write(s_output_dev, (uint8_t *)rec_buf + played_bytes, chunk);
        played_bytes += chunk;
    }

    ESP_LOGI(TAG, "record-play: done, played %lu bytes", (unsigned long)played_bytes);
    heap_caps_free(rec_buf);
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, false);
    vTaskDelete(NULL);
}

esp_err_t board_laiwfs300_audio_record_play_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_output_dev, 100), TAG, "vol max");

    BaseType_t created = xTaskCreate(audio_record_play_task, "audio_rec", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);
    if (pdPASS != created) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "record-play test started (10s record, 10s play)");
    return ESP_OK;
}

esp_err_t board_laiwfs300_audio_read_raw(int16_t *buf, size_t frames, uint8_t channels)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == buf || 0 == frames || 0 == channels) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = frames * channels * sizeof(int16_t);
    return esp_codec_dev_read(s_input_dev, buf, bytes);
}

esp_codec_dev_handle_t board_laiwfs300_audio_get_output_dev(void)
{
    return s_output_dev;
}

esp_codec_dev_handle_t board_laiwfs300_audio_get_input_dev(void)
{
    return s_input_dev;
}

esp_err_t board_laiwfs300_audio_read_tdm_4ch(int16_t *buf, size_t frames)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == buf || 0 == frames) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes_to_read = frames * 4 * sizeof(int16_t);
    size_t bytes_read = 0;
    return i2s_channel_read(s_rx_chan, buf, bytes_to_read, &bytes_read, portMAX_DELAY);
}

esp_err_t board_laiwfs300_audio_open_input_all_channels(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_rx_chan), TAG, "disable rx");

    i2s_tdm_slot_config_t slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_STEREO,
        .slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .ws_width = I2S_TDM_AUTO_WS_WIDTH,
        .ws_pol = false,
        .bit_shift = true,
        .left_align = false,
        .big_endian = false,
        .bit_order_lsb = false,
        .skip_mask = false,
        .total_slot = 4,
    };
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_tdm_slot(s_rx_chan, &slot_cfg), TAG, "reconfig tdm 4ch");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "re-enable rx");

    ESP_LOGI(TAG, "RX TDM reconfigured: 4 slots, raw I2S read mode");
    return ESP_OK;
}
