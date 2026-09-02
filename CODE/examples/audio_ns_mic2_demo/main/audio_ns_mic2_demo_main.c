#include "board_laiwfs300.h"
#include "audio_processor.h"
#include "board_pins.h"
#include "bus_i2c.h"
#include "io_expander.h"

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

static const char *TAG = "mic2_diag";

#define SAMPLE_RATE      BOARD_LAIWFS300_I2S_SAMPLE_RATE
#define TDM_CHANNELS     4
#define ES7210_ADDR      0x40

static i2c_master_dev_handle_t s_es7210_dev = NULL;

static esp_err_t es7210_read_reg(uint8_t reg, uint8_t *val)
{
    esp_err_t ret = i2c_master_transmit_receive(s_es7210_dev, &reg, 1, val, 1, 100);
    return ret;
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
        {0x00, "RESET"},
        {0x01, "CLK_ON_OFF"},
        {0x02, "MCLK_CTL"},
        {0x06, "DIGITAL_PDN"},
        {0x07, "ADC_OSR"},
        {0x08, "MODE_CFG"},
        {0x09, "TCT0_CHPINI"},
        {0x0A, "TCT1_CHPINI"},
        {0x10, "ADC12_HPF2"},
        {0x11, "ADC12_HPF1"},
        {0x12, "ADC34_HPF2"},
        {0x13, "ADC34_HPF1"},
        {0x14, "ADC1_MAX_GAIN"},
        {0x15, "ADC2_MAX_GAIN"},
        {0x16, "ADC3_MAX_GAIN"},
        {0x17, "ADC1_DIRECT_GAIN"},
        {0x18, "ADC2_DIRECT_GAIN"},
        {0x19, "ADC3_DIRECT_GAIN"},
        {0x1A, "ADC4_DIRECT_GAIN"},
        {0x1B, "ADC_MUTE"},
        {0x20, "SDP_CFG"},
        {0x21, "SDP_TIMING"},
        {0x40, "ANALOG_SYS"},
        {0x41, "MICBIAS_CTL"},
        {0x42, "MIC12_PDN"},
        {0x43, "MIC34_PDN"},
        {0x44, "MIC12_BIAS"},
        {0x45, "MIC34_BIAS"},
        {0x46, "MIC1_GAIN"},
        {0x47, "MIC2_GAIN"},
        {0x48, "MIC3_GAIN"},
        {0x49, "MIC4_GAIN"},
        {0x4A, "MIC1_LP"},
        {0x4B, "MIC2_LP"},
        {0x4C, "MIC3_LP"},
        {0x4D, "MIC4_LP"},
    };

    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        uint8_t val = 0;
        esp_err_t ret = es7210_read_reg(regs[i].reg, &val);
        if (ESP_OK == ret) {
            ESP_LOGI(TAG, "  [0x%02X] %-20s = 0x%02X", regs[i].reg, regs[i].name, val);
        } else {
            ESP_LOGW(TAG, "  [0x%02X] %-20s = READ FAILED", regs[i].reg, regs[i].name);
        }
    }
}

static void print_tdm_raw_samples(int16_t *tdm_buf, size_t frames, size_t print_count)
{
    ESP_LOGI(TAG, "===== TDM Raw Samples (first %u frames) =====", (unsigned)print_count);
    ESP_LOGI(TAG, "  Frame | Slot0(MIC1) | Slot1(MIC2) | Slot2(REF)  | Slot3(MIC4)");
    ESP_LOGI(TAG, "  ------|-------------|-------------|-------------|------------");
    for (size_t i = 0; i < print_count && i < frames; i++) {
        ESP_LOGI(TAG, "  %5u | %11d | %11d | %11d | %11d",
                 (unsigned)i,
                 tdm_buf[i * TDM_CHANNELS + 0],
                 tdm_buf[i * TDM_CHANNELS + 1],
                 tdm_buf[i * TDM_CHANNELS + 2],
                 tdm_buf[i * TDM_CHANNELS + 3]);
    }
}

static void compute_slot_stats(int16_t *tdm_buf, size_t frames)
{
    int64_t sum[4] = {0};
    int32_t peak[4] = {0};
    int32_t min_v[4] = {32767, 32767, 32767, 32767};
    int32_t max_v[4] = {-32768, -32768, -32768, -32768};

    for (size_t i = 0; i < frames; i++) {
        for (int ch = 0; ch < 4; ch++) {
            int16_t s = tdm_buf[i * TDM_CHANNELS + ch];
            sum[ch] += (int64_t)s * s;
            int32_t a = s < 0 ? -s : s;
            if (a > peak[ch]) peak[ch] = a;
            if (s < min_v[ch]) min_v[ch] = s;
            if (s > max_v[ch]) max_v[ch] = s;
        }
    }

    ESP_LOGI(TAG, "===== TDM Slot Statistics (%u frames) =====", (unsigned)frames);
    for (int ch = 0; ch < 4; ch++) {
        uint32_t rms = (uint32_t)sqrtf((float)sum[ch] / (float)frames);
        ESP_LOGI(TAG, "  Slot%d: RMS=%5lu  peak=%5ld  min=%6ld  max=%6ld",
                 ch, (unsigned long)rms, (long)peak[ch], (long)min_v[ch], (long)max_v[ch]);
    }
}

static void mic2_diag_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "waiting 5s for serial connection...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MIC2 Hardware Diagnostic");
    ESP_LOGI(TAG, "========================================");

    /* --- Phase 1: ES7210 I2C register dump --- */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[Phase 1] ES7210 Register Dump (before audio init)");

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES7210_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_es7210_dev);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Cannot add ES7210 I2C device: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    dump_es7210_registers();

    i2c_master_bus_rm_device(s_es7210_dev);
    s_es7210_dev = NULL;

    /* --- Phase 2: Initialize audio and dump registers again --- */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[Phase 2] Initialize audio, then re-read ES7210 registers");

    ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "audio init FAILED: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = board_laiwfs300_audio_open_input_all_channels();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "open_input_all_channels FAILED");
        vTaskDelete(NULL);
        return;
    }

    ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_es7210_dev);
    if (ESP_OK == ret) {
        dump_es7210_registers();
        i2c_master_bus_rm_device(s_es7210_dev);
        s_es7210_dev = NULL;
    }

    /* --- Phase 3: Read TDM raw data and print --- */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[Phase 3] TDM Raw Data Capture (4 channels)");
    ESP_LOGI(TAG, "Please speak or make noise during this phase...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    const size_t capture_frames = 2048;
    int16_t *tdm_buf = heap_caps_malloc(capture_frames * TDM_CHANNELS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (NULL == tdm_buf) {
        ESP_LOGE(TAG, "alloc failed");
        vTaskDelete(NULL);
        return;
    }

    size_t total_read = 0;
    while (total_read < capture_frames) {
        size_t chunk = 512;
        if (total_read + chunk > capture_frames) chunk = capture_frames - total_read;
        ret = board_laiwfs300_audio_read_tdm_4ch(&tdm_buf[total_read * TDM_CHANNELS], chunk);
        if (ESP_OK != ret) break;
        total_read += chunk;
    }

    print_tdm_raw_samples(tdm_buf, total_read, 16);
    compute_slot_stats(tdm_buf, total_read);

    /* --- Phase 4: Try setting MIC2 PGA explicitly and re-read --- */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[Phase 4] Force MIC2 PGA=48dB, re-capture");

    ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_es7210_dev);
    if (ESP_OK == ret) {
        es7210_write_reg(0x18, 0x60);
        ESP_LOGI(TAG, "  Wrote 0x18 (ADC2_DIRECT_GAIN) = 0x60 (48dB)");

        uint8_t val = 0;
        es7210_read_reg(0x18, &val);
        ESP_LOGI(TAG, "  Readback 0x18 = 0x%02X", val);

        es7210_read_reg(0x42, &val);
        ESP_LOGI(TAG, "  MIC12_PDN (0x42) = 0x%02X", val);
        es7210_read_reg(0x43, &val);
        ESP_LOGI(TAG, "  MIC34_PDN (0x43) = 0x%02X", val);

        es7210_write_reg(0x42, 0x00);
        ESP_LOGI(TAG, "  Wrote 0x42 (MIC12_PDN) = 0x00 (all powered on)");

        es7210_read_reg(0x1B, &val);
        ESP_LOGI(TAG, "  ADC_MUTE (0x1B) = 0x%02X", val);
        if (val != 0x00) {
            es7210_write_reg(0x1B, 0x00);
            ESP_LOGI(TAG, "  Wrote 0x1B (ADC_MUTE) = 0x00 (all unmuted)");
        }

        i2c_master_bus_rm_device(s_es7210_dev);
        s_es7210_dev = NULL;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "  Re-capturing after PGA/PDN fix...");
    ESP_LOGI(TAG, "  Speak now!");
    vTaskDelay(pdMS_TO_TICKS(2000));

    total_read = 0;
    while (total_read < capture_frames) {
        size_t chunk = 512;
        if (total_read + chunk > capture_frames) chunk = capture_frames - total_read;
        ret = board_laiwfs300_audio_read_tdm_4ch(&tdm_buf[total_read * TDM_CHANNELS], chunk);
        if (ESP_OK != ret) break;
        total_read += chunk;
    }

    print_tdm_raw_samples(tdm_buf, total_read, 16);
    compute_slot_stats(tdm_buf, total_read);

    /* --- Phase 5: Swap test - read MIC2 PGA vs MIC1 PGA --- */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[Phase 5] Swap test: set MIC1 PGA=0dB, MIC2 PGA=48dB");

    ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_es7210_dev);
    if (ESP_OK == ret) {
        es7210_write_reg(0x17, 0x00);
        es7210_write_reg(0x18, 0x60);
        ESP_LOGI(TAG, "  MIC1 PGA=0dB (0x17=0x00), MIC2 PGA=48dB (0x18=0x60)");

        uint8_t v17 = 0, v18 = 0;
        es7210_read_reg(0x17, &v17);
        es7210_read_reg(0x18, &v18);
        ESP_LOGI(TAG, "  Readback: 0x17=0x%02X, 0x18=0x%02X", v17, v18);

        i2c_master_bus_rm_device(s_es7210_dev);
        s_es7210_dev = NULL;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "  Speak now for swap test!");
    vTaskDelay(pdMS_TO_TICKS(2000));

    total_read = 0;
    while (total_read < capture_frames) {
        size_t chunk = 512;
        if (total_read + chunk > capture_frames) chunk = capture_frames - total_read;
        ret = board_laiwfs300_audio_read_tdm_4ch(&tdm_buf[total_read * TDM_CHANNELS], chunk);
        if (ESP_OK != ret) break;
        total_read += chunk;
    }

    compute_slot_stats(tdm_buf, total_read);

    /* --- Summary --- */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  DIAGNOSTIC COMPLETE");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Check above for:");
    ESP_LOGI(TAG, "  1. ES7210 reg dump: MIC2 PDN/mute/PGA settings");
    ESP_LOGI(TAG, "  2. TDM slot data: is MIC2 always 0 or in wrong slot?");
    ESP_LOGI(TAG, "  3. After forcing PGA/PDN: does MIC2 come alive?");
    ESP_LOGI(TAG, "  4. Swap test: if MIC1 goes quiet with PGA=0, confirms");
    ESP_LOGI(TAG, "     register writes work; if MIC2 still dead, it's HW.");

    heap_caps_free(tdm_buf);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "MIC2 Hardware Diagnostic Firmware");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    xTaskCreate(mic2_diag_task, "mic2_diag", 8192, NULL, tskIDLE_PRIORITY + 3, NULL);
}
