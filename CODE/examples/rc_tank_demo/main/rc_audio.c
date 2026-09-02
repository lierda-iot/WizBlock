/**
 * @file rc_audio.c
 * @brief RC Tank Demo - 音频层实现
 *
 * P4: 遥控器 SW3 录音→Opus 编码→TCP 发送
 *     坦克 TCP 接收→Opus 解码→播放
 */
#include "rc_audio.h"
#include "rc_net.h"
#include "rc_tank_common.h"
#include "board_laiwfs300.h"
#include "board_adc.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>

static const char *TAG = "rc_audio";

/* ========== 通用定义 ========== */
#define OPUS_FRAME_MS       60   /* 设计文档规定 60ms */
#define OPUS_FRAME_SAMPLES  (RC_AUDIO_SAMPLE_RATE * OPUS_FRAME_MS / 1000)  // 960 samples
#define MAX_OPUS_FRAME_SIZE 512  /* Opus 最大编码输出（保守） */

#if defined(CONFIG_RC_TANK_ROLE_REMOTE)

/* ========== 遥控器侧: 录音+编码+发送 ========== */

#include "opus_codec.h"

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static esp_codec_dev_handle_t s_input_dev = NULL;
static bool s_opus_encoder_initialized = false;  // P2.1: 跟踪 Opus 编码器状态

esp_err_t rc_audio_record_init(void)
{
    ESP_LOGI(TAG, "Initializing audio recording (ES7210 + SW3)");

    // 初始化板级音频（I2C + I2S + codec）
    esp_err_t ret = board_laiwfs300_audio_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board audio init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = board_laiwfs300_audio_open_input_all_channels();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio input open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_input_dev = board_laiwfs300_audio_get_input_dev();
    if (!s_input_dev) {
        ESP_LOGE(TAG, "Audio input device not available");
        return ESP_ERR_INVALID_STATE;
    }

    // SW3 按键 ADC（GPIO8 = ADC1_CH7）复用板级 ADC1 句柄
    // board_laiwfs300_init() 已初始化 ADC1 单元并配置 CHANNEL_7，
    // 不能再次 adc_oneshot_new_unit（会返回 adc1 already in use）。
    s_adc_handle = board_adc_handle();
    if (!s_adc_handle) {
        ESP_LOGE(TAG, "Board ADC handle not available");
        return ESP_ERR_INVALID_STATE;
    }

    // P2.1 修复: 初始化 Opus 编码器（16kHz/mono/60ms）
    opus_encoder_config_t enc_cfg = {
        .sample_rate = RC_AUDIO_SAMPLE_RATE,
        .channels = 1,
        .frame_duration_ms = OPUS_FRAME_MS,
        .bitrate = 16000,
        .complexity = 5,
        .enable_vbr = true,
        .enable_dtx = false,
    };
    ret = opus_codec_encoder_init(&enc_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Opus encoder init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_opus_encoder_initialized = true;

    ESP_LOGI(TAG, "Audio recording initialized");
    return ESP_OK;
}

bool rc_audio_sw3_pressed(void)
{
    if (!s_adc_handle) return false;

    int adc_val = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, ADC_CHANNEL_7, &adc_val);
    if (ret != ESP_OK) return false;

    return (adc_val < 3000);  // 按下阈值
}

esp_err_t rc_audio_record_and_send(void)
{
    ESP_LOGI(TAG, "Recording started (SW3 pressed)");

    // P2.1 修复: 编码器已在 init 中初始化，检查状态
    if (!s_opus_encoder_initialized) {
        ESP_LOGE(TAG, "Opus encoder not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // P1.3 修复: 严格的内存分配和清理
    size_t pcm_buf_size = OPUS_FRAME_SAMPLES * sizeof(int16_t);
    int16_t *pcm_buf = heap_caps_malloc(pcm_buf_size, MALLOC_CAP_SPIRAM);
    if (!pcm_buf) {
        ESP_LOGE(TAG, "PCM buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    uint8_t *opus_buf = heap_caps_malloc(MAX_OPUS_FRAME_SIZE, MALLOC_CAP_SPIRAM);
    if (!opus_buf) {
        ESP_LOGE(TAG, "Opus buffer alloc failed");
        free(pcm_buf);
        return ESP_ERR_NO_MEM;
    }

    size_t max_audio_size = sizeof(rc_audio_header_t) +
                            (RC_AUDIO_MAX_MS / OPUS_FRAME_MS) * (MAX_OPUS_FRAME_SIZE + 2);
    uint8_t *audio_pkt = heap_caps_malloc(max_audio_size, MALLOC_CAP_SPIRAM);
    if (!audio_pkt) {
        ESP_LOGE(TAG, "Audio packet alloc failed");
        free(opus_buf);
        free(pcm_buf);
        return ESP_ERR_NO_MEM;
    }

    // 录音主循环
    rc_audio_header_t *hdr = (rc_audio_header_t *)audio_pkt;
    hdr->magic = RC_AUDIO_MAGIC;
    hdr->sample_rate = RC_AUDIO_SAMPLE_RATE;
    hdr->length = 0;

    uint8_t *opus_write = audio_pkt + sizeof(rc_audio_header_t);
    uint32_t total_ms = 0;
    uint32_t frame_count = 0;

    esp_err_t ret = ESP_OK;
    while (rc_audio_sw3_pressed() && total_ms < RC_AUDIO_MAX_MS) {
        // 读取一帧 PCM（960 samples @ 16kHz = 60ms）
        ret = board_laiwfs300_audio_read_raw(pcm_buf, OPUS_FRAME_SAMPLES, 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Audio read failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Opus 编码
        size_t opus_len = 0;
        ret = opus_codec_encode(pcm_buf, OPUS_FRAME_SAMPLES, opus_buf, &opus_len);
        if (ret != ESP_OK || opus_len == 0) {
            ESP_LOGW(TAG, "Opus encode failed");
            continue;
        }

        // 写入帧长度前缀 + Opus 数据
        if (hdr->length + opus_len + 2 > max_audio_size - sizeof(rc_audio_header_t)) {
            ESP_LOGW(TAG, "Audio packet buffer full");
            break;
        }

        uint16_t frame_len = (uint16_t)opus_len;
        memcpy(opus_write, &frame_len, 2);
        opus_write += 2;
        memcpy(opus_write, opus_buf, opus_len);
        opus_write += opus_len;
        hdr->length += (2 + opus_len);

        frame_count++;
        total_ms += OPUS_FRAME_MS;
    }

    // 检查最短时长
    if (total_ms < RC_AUDIO_MIN_MS) {
        ESP_LOGW(TAG, "Recording too short (%lu ms < %d ms)", (unsigned long)total_ms, RC_AUDIO_MIN_MS);
        free(audio_pkt);
        free(opus_buf);
        free(pcm_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Recording done: %lu ms, %lu frames, %lu bytes",
             (unsigned long)total_ms, (unsigned long)frame_count, (unsigned long)hdr->length);

    // 发送完整语音包
    size_t send_len = sizeof(rc_audio_header_t) + hdr->length;
    ret = rc_net_audio_send(audio_pkt, send_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio send failed: %s", esp_err_to_name(ret));
    }

    free(audio_pkt);
    free(opus_buf);
    free(pcm_buf);

    return ret;
}

#endif  // CONFIG_RC_TANK_ROLE_REMOTE

#if defined(CONFIG_RC_TANK_ROLE_TANK)

/* ========== 坦克侧: 接收+解码+播放 ========== */

#include "opus_codec.h"

static esp_codec_dev_handle_t s_output_dev = NULL;
static QueueHandle_t s_audio_queue = NULL;
static TaskHandle_t s_audio_rx_task = NULL;
static TaskHandle_t s_audio_play_task = NULL;

typedef struct {
    uint8_t *data;
    size_t len;
} audio_packet_t;

esp_err_t rc_audio_play_init(void)
{
    ESP_LOGI(TAG, "Initializing audio playback");

    // 初始化板级音频（I2C + I2S + codec）
    esp_err_t ret = board_laiwfs300_audio_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board audio init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_output_dev = board_laiwfs300_audio_get_output_dev();
    if (!s_output_dev) {
        ESP_LOGE(TAG, "Audio output device not available");
        return ESP_ERR_INVALID_STATE;
    }

    // 设置音量 70%（board_laiwfs300_audio_init 内部已打开输出设备，无需再次 open）
    ret = esp_codec_dev_set_out_vol(s_output_dev, 70);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set volume failed: %s", esp_err_to_name(ret));
    }

    // 创建音频播放队列（最多缓存 3 段）
    s_audio_queue = xQueueCreate(3, sizeof(audio_packet_t));
    if (!s_audio_queue) {
        ESP_LOGE(TAG, "Audio queue create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Audio playback initialized");
    return ESP_OK;
}

// 音频播放任务
static void audio_play_task(void *arg)
{
    (void)arg;

    // 初始化 Opus 解码器
    opus_decoder_config_t dec_cfg = {
        .sample_rate = RC_AUDIO_SAMPLE_RATE,
        .channels = 1,
        .frame_duration_ms = OPUS_FRAME_MS,
    };
    esp_err_t ret = opus_codec_decoder_init(&dec_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Opus decoder init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    // 分配解码缓冲
    size_t pcm_buf_size = OPUS_FRAME_SAMPLES * sizeof(int16_t);
    int16_t *pcm_buf = heap_caps_malloc(pcm_buf_size, MALLOC_CAP_SPIRAM);
    if (!pcm_buf) {
        ESP_LOGE(TAG, "PCM buffer alloc failed");
        opus_codec_deinit();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Audio play task started");

    while (1) {
        audio_packet_t pkt = {0};
        if (xQueueReceive(s_audio_queue, &pkt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!pkt.data || pkt.len < sizeof(rc_audio_header_t)) {
            ESP_LOGW(TAG, "Invalid audio packet");
            free(pkt.data);
            continue;
        }

        // 解析头部
        rc_audio_header_t *hdr = (rc_audio_header_t *)pkt.data;
        if (hdr->magic != RC_AUDIO_MAGIC || hdr->sample_rate != RC_AUDIO_SAMPLE_RATE) {
            ESP_LOGW(TAG, "Audio header invalid: magic=0x%04X sr=%u",
                     hdr->magic, (unsigned)hdr->sample_rate);
            free(pkt.data);
            continue;
        }

        // 逐帧解码并播放
        uint8_t *opus_ptr = pkt.data + sizeof(rc_audio_header_t);
        uint8_t *opus_end = pkt.data + pkt.len;
        uint32_t frame_count = 0;

        while (opus_ptr + 2 <= opus_end) {
            uint16_t frame_len = 0;
            memcpy(&frame_len, opus_ptr, 2);
            opus_ptr += 2;

            if (opus_ptr + frame_len > opus_end) {
                ESP_LOGW(TAG, "Opus frame truncated");
                break;
            }

            // Opus 解码
            size_t out_samples = 0;
            ret = opus_codec_decode(opus_ptr, frame_len, pcm_buf, &out_samples);
            if (ret != ESP_OK || out_samples == 0) {
                ESP_LOGW(TAG, "Opus decode failed");
                opus_ptr += frame_len;
                continue;
            }

            // 播放（通过 esp_codec_dev）
            ret = esp_codec_dev_write(s_output_dev, pcm_buf, out_samples * sizeof(int16_t));
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Codec write failed: %s", esp_err_to_name(ret));
            }

            opus_ptr += frame_len;
            frame_count++;
        }

        ESP_LOGI(TAG, "Played audio: %lu frames", (unsigned long)frame_count);
        free(pkt.data);
    }
}

// 音频接收任务
static void audio_rx_task(void *arg)
{
    (void)arg;

    size_t max_pkt_size = sizeof(rc_audio_header_t) +
                          (RC_AUDIO_MAX_MS / OPUS_FRAME_MS) * (MAX_OPUS_FRAME_SIZE + 2);
    uint8_t *recv_buf = heap_caps_malloc(max_pkt_size, MALLOC_CAP_SPIRAM);
    if (!recv_buf) {
        ESP_LOGE(TAG, "Audio RX buffer alloc failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Audio RX task started");

    while (1) {
        size_t recv_len = 0;
        esp_err_t ret = rc_net_audio_recv(recv_buf, max_pkt_size, &recv_len);
        if (ret != ESP_OK || recv_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // 复制到队列（播放任务负责释放）
        uint8_t *pkt_copy = heap_caps_malloc(recv_len, MALLOC_CAP_SPIRAM);
        if (!pkt_copy) {
            ESP_LOGW(TAG, "Packet copy alloc failed");
            continue;
        }
        memcpy(pkt_copy, recv_buf, recv_len);

        audio_packet_t pkt = {.data = pkt_copy, .len = recv_len};
        if (xQueueSend(s_audio_queue, &pkt, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Audio queue full, dropping packet");
            free(pkt_copy);
        }
    }
}

esp_err_t rc_audio_play_start(void)
{
    // P2.5: 防止重复创建任务
    if (s_audio_rx_task != NULL || s_audio_play_task != NULL) {
        ESP_LOGW(TAG, "Audio tasks already running");
        return ESP_OK;
    }

    BaseType_t r1 = xTaskCreate(audio_rx_task, "audio_rx", 4096, NULL,
                                 configMAX_PRIORITIES - 3, &s_audio_rx_task);
    if (r1 != pdPASS) {
        ESP_LOGE(TAG, "audio_rx task create failed");
        s_audio_rx_task = NULL;
        return ESP_FAIL;
    }

    BaseType_t r2 = xTaskCreate(audio_play_task, "audio_play", 4096, NULL,
                                 configMAX_PRIORITIES - 3, &s_audio_play_task);
    if (r2 != pdPASS) {
        ESP_LOGE(TAG, "audio_play task create failed");
        // P2.5 修复: 清理已创建的 audio_rx 任务
        vTaskDelete(s_audio_rx_task);
        s_audio_rx_task = NULL;
        s_audio_play_task = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio playback tasks started");
    return ESP_OK;
}

#endif  // CONFIG_RC_TANK_ROLE_TANK

/* ========== 跨角色占位 ========== */

#if defined(CONFIG_RC_TANK_ROLE_TANK)
esp_err_t rc_audio_record_init(void) { return ESP_ERR_NOT_SUPPORTED; }
bool rc_audio_sw3_pressed(void) { return false; }
esp_err_t rc_audio_record_and_send(void) { return ESP_ERR_NOT_SUPPORTED; }
#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)
esp_err_t rc_audio_play_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t rc_audio_play_start(void) { return ESP_ERR_NOT_SUPPORTED; }
#endif
