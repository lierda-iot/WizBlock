/**
 * AEC Demo: 实时连续透传（边录边播）
 *
 * Round 17: 关闭echo_sub（确认无效且加噪）+ 提升GAIN_TARGET(26000→28000)
 *   Round 16 实测：echo_sub全为负值（-10/-7/-25/-24），单tap方案确认失败
 *   关闭echo_sub应改善尾音（去掉加噪源），GAIN_MAX保持3不增加环路风险
 *   GAIN_TARGET提升增加输出音量
 */

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

static const char *TAG = "audio_aec_demo";

#define SAMPLE_RATE         BOARD_LAIWFS300_I2S_SAMPLE_RATE
#define OUTPUT_VOL          60
#define GAIN_TARGET         28000
#define GAIN_MAX            3
#define MIC_PGA_DB          48.0f
#define STARTUP_DELAY_MS    10000
#define TDM_CHANNELS        4

#define DELAY_SAMPLES       1600
#define DELAY_BUF_SIZE      8192

#define GATE_OPEN_THRESH    500
#define GATE_CLOSE_THRESH   250
#define GATE_HOLD_CHUNKS    3
#define GATE_COOLDOWN       10

#define PLAYED_RING_SIZE    4096
#define ECHO_DELAY_SAMPLES  160
#define ECHO_ALPHA_NUM      0
#define ECHO_ALPHA_DEN      100

static esp_codec_dev_handle_t s_out_dev;
static size_t s_feed_chunk = 0;
static size_t s_fetch_chunk = 0;
static volatile bool s_running = false;

static int16_t *s_delay_buf;
static volatile uint32_t s_delay_wr = 0;
static volatile uint32_t s_delay_rd = 0;
static volatile uint32_t s_delay_count = 0;

static int16_t *s_played_ring;
static volatile uint32_t s_played_wr = 0;

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

    ESP_LOGI(TAG, "[feed] started, chunk=%u, echo_sub: delay=%d alpha=%d/%d",
             (unsigned)s_feed_chunk, ECHO_DELAY_SAMPLES, ECHO_ALPHA_NUM, ECHO_ALPHA_DEN);

    int64_t mic_sum = 0;
    int64_t clean_sum = 0;
    uint32_t sample_count = 0;
    TickType_t last_log = xTaskGetTickCount();

    while (s_running) {
        esp_err_t ret = board_laiwfs300_audio_read_tdm_4ch(tdm_buf, tdm_frames);
        if (ESP_OK != ret) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        uint32_t played_wr_snap = s_played_wr;

        for (size_t i = 0; i < tdm_frames; i++) {
            int16_t mic1 = tdm_buf[i * TDM_CHANNELS + 0];
            int16_t ref  = tdm_buf[i * TDM_CHANNELS + 1];
            int16_t mic2 = tdm_buf[i * TDM_CHANNELS + 2];

            int32_t echo_idx = (int32_t)played_wr_snap - ECHO_DELAY_SAMPLES - (int32_t)(tdm_frames - i);
            int16_t echo_ref = 0;
            if (echo_idx >= 0) {
                echo_ref = s_played_ring[(uint32_t)echo_idx % PLAYED_RING_SIZE];
            }

            int32_t mic_clean = (int32_t)mic1 - ((int32_t)echo_ref * ECHO_ALPHA_NUM / ECHO_ALPHA_DEN);
            if (mic_clean > 32767) mic_clean = 32767;
            if (mic_clean < -32768) mic_clean = -32768;

            feed_buf[i * 3]     = (int16_t)mic_clean;
            feed_buf[i * 3 + 1] = mic2;
            feed_buf[i * 3 + 2] = ref;

            mic_sum += (int64_t)mic1 * mic1;
            clean_sum += mic_clean * mic_clean;
            sample_count++;
        }

        audio_processor_feed(feed_buf, s_feed_chunk);
        vTaskDelay(1);

        TickType_t now = xTaskGetTickCount();
        if ((now - last_log) >= pdMS_TO_TICKS(3000)) {
            float rms_raw = (sample_count > 0) ? sqrtf((float)mic_sum / (float)sample_count) : 0;
            float rms_clean = (sample_count > 0) ? sqrtf((float)clean_sum / (float)sample_count) : 0;
            ESP_LOGI(TAG, "[feed] mic_rms=%.0f clean_rms=%.0f echo_sub=%.0f",
                     rms_raw, rms_clean, rms_raw - rms_clean);
            mic_sum = 0;
            clean_sum = 0;
            sample_count = 0;
            last_log = now;
        }
    }

    heap_caps_free(tdm_buf);
    heap_caps_free(feed_buf);
    ESP_LOGI(TAG, "[feed] exited");
    vTaskDelete(NULL);
}

static void fetch_task(void *arg)
{
    (void)arg;
    int16_t *fetch_buf = heap_caps_malloc(s_fetch_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *play_buf = heap_caps_malloc(s_fetch_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (NULL == fetch_buf || NULL == play_buf) {
        ESP_LOGE(TAG, "fetch_task alloc failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "[fetch] started, delay=%dms, gate: open>%d close<%d hold=%d cooldown=%d",
             DELAY_SAMPLES * 1000 / SAMPLE_RATE,
             GATE_OPEN_THRESH, GATE_CLOSE_THRESH, GATE_HOLD_CHUNKS, GATE_COOLDOWN);

    int32_t running_peak = 100;
    int64_t out_sum = 0;
    uint32_t out_count = 0;
    TickType_t last_log = xTaskGetTickCount();

    bool gate_open = false;
    int gate_hold_counter = 0;
    int gate_cooldown_counter = 0;

    while (s_running) {
        size_t fetched = 0;
        bool vad = false;
        esp_err_t ret = audio_processor_fetch(fetch_buf, &fetched, &vad, NULL);
        if (ESP_OK != ret || 0 == fetched) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t wr = s_delay_wr;
        for (size_t i = 0; i < fetched; i++) {
            s_delay_buf[wr % DELAY_BUF_SIZE] = fetch_buf[i];
            wr++;
        }
        s_delay_wr = wr;
        s_delay_count += fetched;

        if (s_delay_count < DELAY_SAMPLES) {
            continue;
        }

        uint32_t rd = s_delay_rd;
        size_t to_play = fetched;
        if (s_delay_count - DELAY_SAMPLES < to_play) {
            to_play = s_delay_count - DELAY_SAMPLES;
        }

        int32_t chunk_peak = 1;
        for (size_t i = 0; i < to_play; i++) {
            int16_t s = s_delay_buf[rd % DELAY_BUF_SIZE];
            rd++;
            int32_t abs_s = (s < 0) ? -s : s;
            if (abs_s > chunk_peak) chunk_peak = abs_s;
            out_sum += (int64_t)s * s;
            out_count++;
        }

        if (chunk_peak > running_peak) {
            running_peak = chunk_peak;
        } else {
            running_peak = running_peak - running_peak / 64;
            if (running_peak < 100) running_peak = 100;
        }

        if (!gate_open) {
            if (gate_cooldown_counter > 0) {
                gate_cooldown_counter--;
            } else if (chunk_peak >= GATE_OPEN_THRESH) {
                gate_open = true;
                gate_hold_counter = GATE_HOLD_CHUNKS;
                ESP_LOGI(TAG, "[gate] OPEN peak=%ld", (long)chunk_peak);
            }
        } else {
            if (chunk_peak >= GATE_CLOSE_THRESH) {
                gate_hold_counter = GATE_HOLD_CHUNKS;
            } else {
                gate_hold_counter--;
                if (gate_hold_counter <= 0) {
                    gate_open = false;
                    gate_cooldown_counter = GATE_COOLDOWN;
                    ESP_LOGI(TAG, "[gate] CLOSED (cooldown=%d)", GATE_COOLDOWN);
                }
            }
        }

        if (gate_open) {
            int32_t gain = GAIN_TARGET / running_peak;
            if (gain > GAIN_MAX) gain = GAIN_MAX;
            if (gain < 1) gain = 1;

            uint32_t rd2 = s_delay_rd;
            uint32_t pwr = s_played_wr;
            for (size_t i = 0; i < to_play; i++) {
                int32_t val = (int32_t)s_delay_buf[rd2 % DELAY_BUF_SIZE] * gain;
                rd2++;
                if (val > 32767) val = 32767;
                if (val < -32768) val = -32768;
                play_buf[i] = (int16_t)val;
                s_played_ring[pwr % PLAYED_RING_SIZE] = (int16_t)val;
                pwr++;
            }
            s_played_wr = pwr;
        } else {
            memset(play_buf, 0, to_play * sizeof(int16_t));
            uint32_t pwr = s_played_wr;
            for (size_t i = 0; i < to_play; i++) {
                s_played_ring[pwr % PLAYED_RING_SIZE] = 0;
                pwr++;
            }
            s_played_wr = pwr;
        }

        s_delay_rd += to_play;
        s_delay_count -= to_play;

        esp_codec_dev_write(s_out_dev, play_buf, to_play * sizeof(int16_t));

        TickType_t now = xTaskGetTickCount();
        if ((now - last_log) >= pdMS_TO_TICKS(3000)) {
            float rms = (out_count > 0) ? sqrtf((float)out_sum / (float)out_count) : 0;
            ESP_LOGI(TAG, "[fetch] rms=%.0f peak=%ld gate=%s hold=%d cool=%d",
                     rms, (long)running_peak, gate_open ? "OPEN" : "CLOSED",
                     gate_hold_counter, gate_cooldown_counter);
            out_sum = 0;
            out_count = 0;
            last_log = now;
        }
    }

    heap_caps_free(fetch_buf);
    heap_caps_free(play_buf);
    ESP_LOGI(TAG, "[fetch] exited");
    vTaskDelete(NULL);
}

static void set_mic_pga(void)
{
    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    i2c_device_config_t es7210_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x40,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t es7210_dev = NULL;
    esp_err_t ret = i2c_master_bus_add_device(bus, &es7210_cfg, &es7210_dev);
    if (ESP_OK == ret) {
        uint8_t reg17[2] = {0x17, 0x60};
        uint8_t reg18[2] = {0x18, 0x60};
        i2c_master_transmit(es7210_dev, reg17, 2, 100);
        i2c_master_transmit(es7210_dev, reg18, 2, 100);
        ESP_LOGI(TAG, "ES7210 MIC1+MIC2 PGA set to 48dB");
        i2c_master_bus_rm_device(es7210_dev);
    } else {
        ESP_LOGW(TAG, "ES7210 direct I2C failed, fallback to 37.5dB");
        esp_codec_dev_handle_t in_dev = board_laiwfs300_audio_get_input_dev();
        esp_codec_dev_set_in_channel_gain(in_dev,
            ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1), 37.5f);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== AEC Demo Round 14: NS + gate + playback echo cancel ===");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "waiting %ds...", STARTUP_DELAY_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(STARTUP_DELAY_MS));

    ESP_LOGI(TAG, "init audio...");
    ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "audio init FAILED: %s", esp_err_to_name(ret));
        return;
    }
    s_out_dev = board_laiwfs300_audio_get_output_dev();

    set_mic_pga();

    ret = board_laiwfs300_audio_open_input_all_channels();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "open_input_all_channels FAILED");
        return;
    }

    audio_processor_config_t afe_cfg = {
        .mic_channels = 2,
        .ref_channels = 1,
        .enable_ns    = true,
        .enable_aec   = true,
        .enable_vad   = false,
        .aec_mode     = 3,
    };
    ret = audio_processor_init(&afe_cfg);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "AFE init FAILED");
        return;
    }
    s_feed_chunk = audio_processor_get_feed_chunksize();
    s_fetch_chunk = audio_processor_get_fetch_chunksize();

    audio_processor_disable_aec();
    audio_processor_reset_buffer();

    s_delay_buf = heap_caps_calloc(DELAY_BUF_SIZE, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_played_ring = heap_caps_calloc(PLAYED_RING_SIZE, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (NULL == s_delay_buf || NULL == s_played_ring) {
        ESP_LOGE(TAG, "buffer alloc failed");
        return;
    }

    amp_enable();
    esp_codec_dev_set_out_vol(s_out_dev, OUTPUT_VOL);

    ESP_LOGI(TAG, "Round 14 config:");
    ESP_LOGI(TAG, "  PGA=48dB, VOL=%d%%, GAIN_TARGET=%d, GAIN_MAX=%d",
             OUTPUT_VOL, GAIN_TARGET, GAIN_MAX);
    ESP_LOGI(TAG, "  Delay=%dms, AEC=OFF, NS=NSNet2",
             DELAY_SAMPLES * 1000 / SAMPLE_RATE);
    ESP_LOGI(TAG, "  Gate: open>%d close<%d hold=%d cooldown=%d",
             GATE_OPEN_THRESH, GATE_CLOSE_THRESH, GATE_HOLD_CHUNKS, GATE_COOLDOWN);
    ESP_LOGI(TAG, "  Echo cancel: played_ring=%d, delay=%d samples(%dms), alpha=%d/%d",
             PLAYED_RING_SIZE, ECHO_DELAY_SAMPLES,
             ECHO_DELAY_SAMPLES * 1000 / SAMPLE_RATE,
             ECHO_ALPHA_NUM, ECHO_ALPHA_DEN);
    ESP_LOGI(TAG, "  MIC-echo -> AFE(NS) -> delay -> gate+cooldown -> gain -> speaker -> played_ring");

    s_running = true;
    xTaskCreate(fetch_task, "aec_fetch", 8192, NULL, tskIDLE_PRIORITY + 3, NULL);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreate(feed_task, "aec_feed", 8192, NULL, tskIDLE_PRIORITY + 2, NULL);
}
