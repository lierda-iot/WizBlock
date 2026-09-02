/*
 * mic_test - ES7210 dual MIC diagnostic for ESP32-S3-Korvo-2 V3
 * Hardware: ES7210 (ADC, I2C 0x40) + ES8311 (DAC, I2C 0x18)
 * GPIO: MCLK=16, BCLK=9, WS=45, DIN=10, DOUT=8, I2C SDA=17 SCL=18, PA=48
 */
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

static const char *TAG = "mic_test";

#define I2S_MCLK_GPIO   16
#define I2S_BCLK_GPIO   9
#define I2S_WS_GPIO     45
#define I2S_DIN_GPIO    10
#define I2S_DOUT_GPIO   8
#define I2C_SDA_GPIO    17
#define I2C_SCL_GPIO    18
#define PA_GPIO         48

#define SAMPLE_RATE     16000
#define TDM_CHANNELS    4
#define ES7210_I2C_ADDR 0x40
#define ES8311_I2C_ADDR 0x18

static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_tx_chan;
static i2s_chan_handle_t s_rx_chan;
static i2c_master_dev_handle_t s_es7210_dev;

static esp_err_t es7210_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_es7210_dev, &reg, 1, val, 1, 100);
}

static esp_err_t es7210_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_es7210_dev, buf, 2, 100);
}

static void dump_es7210_registers(void)
{
    ESP_LOGI(TAG, "===== ES7210 Register Dump =====");
    struct { uint8_t reg; const char *name; } regs[] = {
        {0x00, "RESET"},       {0x01, "CLK_ON_OFF"},  {0x02, "MCLK_CTL"},
        {0x06, "DIGITAL_PDN"}, {0x07, "ADC_OSR"},     {0x08, "MODE_CFG"},
        {0x10, "ADC12_HPF2"},  {0x11, "ADC12_HPF1"},
        {0x14, "ADC1_MAX_GAIN"}, {0x15, "ADC2_MAX_GAIN"},
        {0x17, "ADC1_DIRECT_GAIN"}, {0x18, "ADC2_DIRECT_GAIN"},
        {0x19, "ADC3_DIRECT_GAIN"}, {0x1A, "ADC4_DIRECT_GAIN"},
        {0x1B, "ADC_MUTE"},
        {0x20, "SDP_CFG"},     {0x21, "SDP_TIMING"},
        {0x40, "ANALOG_SYS"}, {0x41, "MICBIAS_CTL"},
        {0x42, "MIC12_PDN"},  {0x43, "MIC34_PDN"},
        {0x44, "MIC12_BIAS"}, {0x45, "MIC34_BIAS"},
        {0x46, "MIC1_GAIN"},  {0x47, "MIC2_GAIN"},
        {0x48, "MIC3_GAIN"},  {0x49, "MIC4_GAIN"},
        {0x4A, "MIC1_LP"},    {0x4B, "MIC2_LP"},
        {0x4C, "MIC3_LP"},    {0x4D, "MIC4_LP"},
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        uint8_t val = 0;
        esp_err_t ret = es7210_read_reg(regs[i].reg, &val);
        if (ESP_OK == ret) {
            ESP_LOGI(TAG, "  [0x%02X] %-18s = 0x%02X", regs[i].reg, regs[i].name, val);
        } else {
            ESP_LOGW(TAG, "  [0x%02X] %-18s = READ FAILED", regs[i].reg, regs[i].name);
        }
    }
}

static void compute_slot_stats(const int16_t *tdm_buf, size_t frames)
{
    int64_t sum[4] = {0};
    int32_t peak[4] = {0};
    for (size_t i = 0; i < frames; i++) {
        for (int ch = 0; ch < 4; ch++) {
            int16_t s = tdm_buf[i * TDM_CHANNELS + ch];
            sum[ch] += (int64_t)s * s;
            int32_t a = s < 0 ? -s : s;
            if (a > peak[ch]) {
                peak[ch] = a;
            }
        }
    }
    ESP_LOGI(TAG, "  Slot Stats (%u frames):", (unsigned)frames);
    for (int ch = 0; ch < 4; ch++) {
        uint32_t rms = (uint32_t)sqrtf((float)sum[ch] / (float)frames);
        ESP_LOGI(TAG, "    Slot%d(MIC%d): RMS=%5lu  peak=%5ld", ch, ch + 1,
                 (unsigned long)rms, (long)peak[ch]);
    }
}

static esp_err_t init_i2c(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES7210_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_es7210_dev), TAG, "es7210 dev");

    ESP_LOGI(TAG, "I2C initialized: SDA=%d SCL=%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    return ESP_OK;
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan), TAG, "new chan");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_BCLK_GPIO,
            .ws   = I2S_WS_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_BCLK_GPIO,
            .ws   = I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN_GPIO,
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "tx std");
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(s_rx_chan, &tdm_cfg), TAG, "rx tdm");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "en tx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "en rx");

    ESP_LOGI(TAG, "I2S0: TX=STD RX=TDM MCLK=%d BCLK=%d WS=%d DIN=%d DOUT=%d rate=%d",
             I2S_MCLK_GPIO, I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DIN_GPIO, I2S_DOUT_GPIO, SAMPLE_RATE);
    return ESP_OK;
}

static esp_err_t init_codec(void)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = s_rx_chan,
        .tx_handle = s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = ES8311_I2C_ADDR * 2,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *out_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = out_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PA_GPIO,
        .use_mclk = true,
    };
    const audio_codec_if_t *out_codec = es8311_codec_new(&es8311_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_codec,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t out_dev = esp_codec_dev_new(&dev_cfg);

    i2c_cfg.addr = ES7210_I2C_ADDR * 2;
    const audio_codec_ctrl_if_t *in_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    const audio_codec_if_t *in_codec = es7210_codec_new(&es7210_cfg);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = in_codec;
    esp_codec_dev_handle_t in_dev = esp_codec_dev_new(&dev_cfg);

    esp_codec_dev_sample_info_t out_fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = SAMPLE_RATE,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(out_dev, &out_fs), TAG, "open out");

    esp_codec_dev_sample_info_t in_fs = {
        .bits_per_sample = 16,
        .channel = 4,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)
                      | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3),
        .sample_rate = SAMPLE_RATE,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(in_dev, &in_fs), TAG, "open in");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_channel_gain(in_dev,
        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1), 30.0),
        TAG, "set gain");

    ESP_LOGI(TAG, "Codec init OK (ES8311 out + ES7210 in, MIC1-4 selected)");
    return ESP_OK;
}

static esp_err_t read_tdm_4ch(int16_t *buf, size_t frames)
{
    size_t bytes_read = 0;
    return i2s_channel_read(s_rx_chan, buf, frames * 4 * sizeof(int16_t), &bytes_read, portMAX_DELAY);
}

static void mic_test_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "waiting 5s for serial...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MIC Test - ESP32-S3-Korvo-2 V3");
    ESP_LOGI(TAG, "========================================");

    ESP_LOGI(TAG, "[Phase 1] ES7210 registers before audio init");
    dump_es7210_registers();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[Phase 2] Init audio (I2S + codec)");
    esp_err_t ret = init_i2s();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    ret = init_codec();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Codec init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "  ES7210 registers after audio init:");
    dump_es7210_registers();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[Phase 3] TDM capture - speak now!");
    vTaskDelay(pdMS_TO_TICKS(2000));

    const size_t cap_frames = 2048;
    int16_t *tdm_buf = malloc(cap_frames * TDM_CHANNELS * sizeof(int16_t));
    if (NULL == tdm_buf) {
        ESP_LOGE(TAG, "alloc failed");
        vTaskDelete(NULL);
        return;
    }

    size_t total = 0;
    while (total < cap_frames) {
        size_t chunk = 512;
        if (total + chunk > cap_frames) { chunk = cap_frames - total; }
        ret = read_tdm_4ch(&tdm_buf[total * TDM_CHANNELS], chunk);
        if (ESP_OK != ret) { break; }
        total += chunk;
    }
    compute_slot_stats(tdm_buf, total);

    ESP_LOGI(TAG, "  First 8 frames raw:");
    for (size_t i = 0; i < 8 && i < total; i++) {
        ESP_LOGI(TAG, "    [%u] %6d %6d %6d %6d", (unsigned)i,
                 tdm_buf[i*4], tdm_buf[i*4+1], tdm_buf[i*4+2], tdm_buf[i*4+3]);
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[Phase 4] Continuous monitoring (10s)");
    for (int round = 0; round < 20; round++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        total = 0;
        while (total < cap_frames) {
            size_t chunk = 512;
            if (total + chunk > cap_frames) { chunk = cap_frames - total; }
            ret = read_tdm_4ch(&tdm_buf[total * TDM_CHANNELS], chunk);
            if (ESP_OK != ret) { break; }
            total += chunk;
        }
        int64_t s0 = 0, s1 = 0;
        for (size_t i = 0; i < total; i++) {
            int16_t v0 = tdm_buf[i * 4];
            int16_t v1 = tdm_buf[i * 4 + 1];
            s0 += (int64_t)v0 * v0;
            s1 += (int64_t)v1 * v1;
        }
        uint32_t rms0 = (uint32_t)sqrtf((float)s0 / (float)total);
        uint32_t rms1 = (uint32_t)sqrtf((float)s1 / (float)total);
        ESP_LOGI(TAG, "  [%2d] MIC1_RMS=%5lu  MIC2_RMS=%5lu", round,
                 (unsigned long)rms0, (unsigned long)rms1);
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MIC TEST COMPLETE");
    ESP_LOGI(TAG, "========================================");

    free(tdm_buf);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "MIC Test for ESP32-S3-Korvo-2 V3");
    ESP_LOGI(TAG, "GPIO: MCLK=%d BCLK=%d WS=%d DIN=%d DOUT=%d I2C_SDA=%d SCL=%d PA=%d",
             I2S_MCLK_GPIO, I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DIN_GPIO, I2S_DOUT_GPIO,
             I2C_SDA_GPIO, I2C_SCL_GPIO, PA_GPIO);

    esp_err_t ret = init_i2c();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        return;
    }

    xTaskCreate(mic_test_task, "mic_test", 8192, NULL, tskIDLE_PRIORITY + 3, NULL);
}
