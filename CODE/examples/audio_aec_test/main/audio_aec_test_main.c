/**
 * Controlled AEC ON/OFF evaluation for the LYWSD03 audio board.
 *
 * Both passes use the same MMR input, tone, timing, NSNet2 and WakeNet setup.
 * The only controlled variable is whether AEC is enabled when AFE is created.
 * MIC1, amplified REF and AFE output are captured in PSRAM for offline metrics.
 *
 * TDM mapping: slot0=MIC1, slot1=REF, slot2=MIC2, slot3=unused.
 */

#include "aec_test_logic.h"
#include "audio_processor.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "bus_i2c.h"
#include "io_expander.h"

#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "audio_aec_test";

#define SAMPLE_RATE                  BOARD_LAIWFS300_I2S_SAMPLE_RATE
#define OUTPUT_VOL                  100U
#define REF_GAIN                    4
#define PLAYBACK_GAIN_MAX           60
#define MIC_PGA_DB                  48.0f
#define WAKENET_THRESHOLD           0.65f
#define STARTUP_DELAY_MS            15000U
#define TEST_TONE_HZ                1000U
#define TONE_AMPLITUDE              12000
#define TONE_DURATION_MS            5000U
#define RECOVERY_DURATION_MS        3000U
#define TDM_CHANNELS                4U
#define TASK_WARMUP_MS              250U
#define TASK_JOIN_TIMEOUT_MS        5000U
#define PASS_GAP_MS                 2000U
#define PASS_START_BEEP_COUNT       1
#define TONE_REFERENCE_AMPLITUDE    1000.0f
#define TWO_PI_F                    6.28318530717958647692f
#define PLAYBACK_TARGET_PEAK        16000
#define PLAYBACK_CHUNK_SAMPLES      256U
#define METRIC_CLIP_PERCENT_SCALE   100.0f
#define AEC_MODE_VOIP_LOW_COST      3

#define TASK_MASK_ALL (AEC_TEST_TASK_FEED | AEC_TEST_TASK_FETCH | AEC_TEST_TASK_TONE)

typedef enum {
    CAPTURE_WINDOW_NONE = 0,
    CAPTURE_WINDOW_TONE,
    CAPTURE_WINDOW_RECOVERY,
} capture_window_t;

typedef enum {
    PASS_RUN_OK = 0,
    PASS_RUN_FAILED_CLEAN,
    PASS_RUN_FAILED_UNSAFE,
} pass_run_status_t;

typedef struct {
    int16_t *mic;
    int16_t *ref;
    int16_t *afe;
    size_t tone_capacity;
    size_t recovery_capacity;
    volatile size_t feed_tone_written;
    volatile size_t feed_recovery_written;
    volatile size_t afe_tone_written;
    volatile size_t afe_recovery_written;
    volatile uint32_t wake_tone_count;
    volatile uint32_t wake_recovery_count;
} pass_capture_t;

typedef struct {
    aec_test_signal_metrics_t mic_tone;
    aec_test_signal_metrics_t ref_tone;
    aec_test_signal_metrics_t afe_tone;
    aec_test_signal_metrics_t mic_recovery;
    aec_test_signal_metrics_t ref_recovery;
    aec_test_signal_metrics_t afe_recovery;
} pass_metrics_t;

static esp_codec_dev_handle_t s_out_dev = NULL;
static size_t s_feed_chunk = 0U;
static size_t s_fetch_chunk = 0U;

static volatile bool s_capture_active = false;
static volatile bool s_tone_active = false;
static volatile bool s_task_failed = false;
static volatile capture_window_t s_capture_window = CAPTURE_WINDOW_NONE;
static pass_capture_t *s_active_capture = NULL;

static SemaphoreHandle_t s_task_done_sem = NULL;
static portMUX_TYPE s_task_exit_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t s_task_exit_mask = 0U;

static void amp_enable(void)
{
    io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                  BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                          BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
}

static void amp_disable(void)
{
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                          BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, false);
}

static void play_beeps(int count)
{
    const uint32_t beep_hz = 3000U;
    const uint32_t beep_ms = 150U;
    const uint32_t gap_ms = 500U;
    const size_t beep_samples = (SAMPLE_RATE * beep_ms) / 1000U;
    int16_t buffer[160];

    amp_enable();
    esp_codec_dev_set_out_vol(s_out_dev, OUTPUT_VOL);
    for (int beep = 0; beep < count; beep++) {
        size_t sample_index = 0U;
        size_t remaining = beep_samples;
        while (remaining > 0U) {
            const size_t chunk = (remaining < 160U) ? remaining : 160U;
            for (size_t i = 0U; i < chunk; i++) {
                const float time_s = (float)(sample_index + i) / (float)SAMPLE_RATE;
                buffer[i] = (int16_t)(8000.0f *
                    sinf(TWO_PI_F * (float)beep_hz * time_s));
            }
            esp_codec_dev_write(s_out_dev, buffer, chunk * sizeof(int16_t));
            sample_index += chunk;
            remaining -= chunk;
        }
        if (beep < (count - 1)) {
            vTaskDelay(pdMS_TO_TICKS(gap_ms));
        }
    }
    amp_disable();
    vTaskDelay(pdMS_TO_TICKS(1000U));
}

static bool pass_capture_alloc(pass_capture_t *capture,
                               size_t tone_samples,
                               size_t recovery_samples)
{
    if (NULL == capture || 0U == tone_samples || 0U == recovery_samples) {
        return false;
    }

    *capture = (pass_capture_t){0};
    const size_t total_samples = tone_samples + recovery_samples;
    const size_t total_bytes = total_samples * sizeof(int16_t);
    capture->mic = heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    capture->ref = heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    capture->afe = heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == capture->mic || NULL == capture->ref || NULL == capture->afe) {
        heap_caps_free(capture->mic);
        heap_caps_free(capture->ref);
        heap_caps_free(capture->afe);
        *capture = (pass_capture_t){0};
        return false;
    }
    capture->tone_capacity = tone_samples;
    capture->recovery_capacity = recovery_samples;
    return true;
}

static void pass_capture_free(pass_capture_t *capture)
{
    if (NULL == capture) {
        return;
    }
    heap_caps_free(capture->mic);
    heap_caps_free(capture->ref);
    heap_caps_free(capture->afe);
    *capture = (pass_capture_t){0};
}

static void capture_feed_sample(int16_t mic, int16_t ref)
{
    pass_capture_t *capture = s_active_capture;
    if (NULL == capture) {
        return;
    }

    const capture_window_t window = s_capture_window;
    if (CAPTURE_WINDOW_TONE == window) {
        const size_t index = capture->feed_tone_written;
        if (index < capture->tone_capacity) {
            capture->mic[index] = mic;
            capture->ref[index] = ref;
            capture->feed_tone_written = index + 1U;
        }
    } else if (CAPTURE_WINDOW_RECOVERY == window) {
        const size_t index = capture->feed_recovery_written;
        if (index < capture->recovery_capacity) {
            const size_t offset = capture->tone_capacity + index;
            capture->mic[offset] = mic;
            capture->ref[offset] = ref;
            capture->feed_recovery_written = index + 1U;
        }
    }
}

static void capture_afe_samples(const int16_t *samples, size_t sample_count)
{
    pass_capture_t *capture = s_active_capture;
    if (NULL == capture || NULL == samples || 0U == sample_count) {
        return;
    }

    const capture_window_t window = s_capture_window;
    if (CAPTURE_WINDOW_TONE == window) {
        size_t writable = capture->tone_capacity - capture->afe_tone_written;
        if (sample_count < writable) {
            writable = sample_count;
        }
        if (writable > 0U) {
            memcpy(capture->afe + capture->afe_tone_written,
                   samples, writable * sizeof(int16_t));
            capture->afe_tone_written += writable;
        }
    } else if (CAPTURE_WINDOW_RECOVERY == window) {
        size_t writable = capture->recovery_capacity - capture->afe_recovery_written;
        if (sample_count < writable) {
            writable = sample_count;
        }
        if (writable > 0U) {
            const size_t offset = capture->tone_capacity + capture->afe_recovery_written;
            memcpy(capture->afe + offset, samples, writable * sizeof(int16_t));
            capture->afe_recovery_written += writable;
        }
    }
}

static void capture_wakeup(void)
{
    pass_capture_t *capture = s_active_capture;
    if (NULL == capture) {
        return;
    }
    if (CAPTURE_WINDOW_TONE == s_capture_window) {
        capture->wake_tone_count++;
    } else if (CAPTURE_WINDOW_RECOVERY == s_capture_window) {
        capture->wake_recovery_count++;
    }
}

static void signal_task_exit(uint8_t task_mask)
{
    taskENTER_CRITICAL(&s_task_exit_lock);
    s_task_exit_mask |= task_mask;
    taskEXIT_CRITICAL(&s_task_exit_lock);
    if (NULL != s_task_done_sem) {
        xSemaphoreGive(s_task_done_sem);
    }
}

static uint8_t get_task_exit_mask(void)
{
    uint8_t mask = 0U;
    taskENTER_CRITICAL(&s_task_exit_lock);
    mask = s_task_exit_mask;
    taskEXIT_CRITICAL(&s_task_exit_lock);
    return mask;
}

static bool wait_for_task_mask(uint8_t expected_mask, uint32_t timeout_ms)
{
    const TickType_t start_tick = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (expected_mask != (get_task_exit_mask() & expected_mask)) {
        const TickType_t elapsed = xTaskGetTickCount() - start_tick;
        if (elapsed >= timeout_ticks) {
            return false;
        }
        const TickType_t remaining = timeout_ticks - elapsed;
        if (pdTRUE != xSemaphoreTake(s_task_done_sem, remaining)) {
            return false;
        }
    }
    return true;
}

static void tone_task(void *arg)
{
    const aec_test_pass_plan_t *plan = (const aec_test_pass_plan_t *)arg;
    const size_t chunk = 128U;
    int16_t buffer[128];
    uint32_t sample_index = 0U;

    if (NULL == plan) {
        s_task_failed = true;
        signal_task_exit(AEC_TEST_TASK_TONE);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "[tone] playing %luHz amplitude=%d",
             (unsigned long)plan->tone_hz, plan->tone_amplitude);
    while (s_tone_active) {
        for (size_t i = 0U; i < chunk; i++) {
            const float time_s = (float)(sample_index + i) / (float)SAMPLE_RATE;
            buffer[i] = (int16_t)((float)plan->tone_amplitude *
                sinf(TWO_PI_F * (float)plan->tone_hz * time_s));
        }
        if (ESP_OK != esp_codec_dev_write(s_out_dev, buffer,
                                          chunk * sizeof(int16_t))) {
            ESP_LOGE(TAG, "[tone] codec write failed");
            s_task_failed = true;
            break;
        }
        sample_index += chunk;
        vTaskDelay(1);
    }

    memset(buffer, 0, sizeof(buffer));
    esp_codec_dev_write(s_out_dev, buffer, sizeof(buffer));
    ESP_LOGI(TAG, "[tone] stopped");
    signal_task_exit(AEC_TEST_TASK_TONE);
    vTaskDelete(NULL);
}

static void feed_task(void *arg)
{
    (void)arg;
    const size_t tdm_frames = s_feed_chunk;
    int16_t *tdm_buffer = heap_caps_malloc(
        tdm_frames * TDM_CHANNELS * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *feed_buffer = heap_caps_malloc(
        tdm_frames * 3U * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == tdm_buffer || NULL == feed_buffer) {
        ESP_LOGE(TAG, "[feed] buffer allocation failed");
        heap_caps_free(tdm_buffer);
        heap_caps_free(feed_buffer);
        s_task_failed = true;
        signal_task_exit(AEC_TEST_TASK_FEED);
        vTaskDelete(NULL);
        return;
    }

    aec_test_signal_accumulator_t mic_stats;
    aec_test_signal_accumulator_t ref_stats;
    aec_test_signal_reset(&mic_stats);
    aec_test_signal_reset(&ref_stats);
    TickType_t last_log = xTaskGetTickCount();

    ESP_LOGI(TAG, "[feed] started chunk=%u REF_GAIN=%d format=MMR",
             (unsigned)s_feed_chunk, REF_GAIN);
    while (s_capture_active) {
        const esp_err_t read_result =
            board_laiwfs300_audio_read_tdm_4ch(tdm_buffer, tdm_frames);
        if (ESP_OK != read_result) {
            vTaskDelay(pdMS_TO_TICKS(1U));
            continue;
        }

        for (size_t i = 0U; i < tdm_frames; i++) {
            const int16_t mic1 = tdm_buffer[(i * TDM_CHANNELS) + 0U];
            int32_t ref_scaled = (int32_t)tdm_buffer[(i * TDM_CHANNELS) + 1U] *
                                 REF_GAIN;
            if (ref_scaled > INT16_MAX) {
                ref_scaled = INT16_MAX;
            } else if (ref_scaled < INT16_MIN) {
                ref_scaled = INT16_MIN;
            }
            const int16_t ref = (int16_t)ref_scaled;
            const int16_t mic2 = tdm_buffer[(i * TDM_CHANNELS) + 2U];

            feed_buffer[(i * 3U) + 0U] = mic1;
            feed_buffer[(i * 3U) + 1U] = mic2;
            feed_buffer[(i * 3U) + 2U] = ref;
            capture_feed_sample(mic1, ref);
            aec_test_signal_add(&mic_stats, mic1, 0, 0);
            aec_test_signal_add(&ref_stats, ref, 0, 0);
        }

        if (ESP_OK != audio_processor_feed(feed_buffer, tdm_frames)) {
            ESP_LOGE(TAG, "[feed] AFE feed failed");
            s_task_failed = true;
            break;
        }

        /* Required by the Round 13 NSNet2 watchdog regression. */
        vTaskDelay(1);

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_log) >= pdMS_TO_TICKS(2000U)) {
            aec_test_signal_metrics_t mic_metrics = {0};
            aec_test_signal_metrics_t ref_metrics = {0};
            aec_test_signal_summarize(&mic_stats, &mic_metrics);
            aec_test_signal_summarize(&ref_stats, &ref_metrics);
            ESP_LOGI(TAG,
                     "[feed] window=%d mic_rms=%.0f ref_rms=%.0f mic_pk=%ld ref_pk=%ld",
                     (int)s_capture_window, (double)mic_metrics.rms,
                     (double)ref_metrics.rms, (long)mic_metrics.peak,
                     (long)ref_metrics.peak);
            aec_test_signal_reset(&mic_stats);
            aec_test_signal_reset(&ref_stats);
            last_log = now;
        }
    }

    heap_caps_free(tdm_buffer);
    heap_caps_free(feed_buffer);
    ESP_LOGI(TAG, "[feed] exited");
    signal_task_exit(AEC_TEST_TASK_FEED);
    vTaskDelete(NULL);
}

static void fetch_task(void *arg)
{
    (void)arg;
    int16_t *fetch_buffer = heap_caps_malloc(
        s_fetch_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == fetch_buffer) {
        ESP_LOGE(TAG, "[fetch] buffer allocation failed");
        s_task_failed = true;
        signal_task_exit(AEC_TEST_TASK_FETCH);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "[fetch] started chunk=%u", (unsigned)s_fetch_chunk);
    while (s_capture_active) {
        size_t fetched = 0U;
        bool vad_active = false;
        bool wakeup = false;
        const esp_err_t fetch_result = audio_processor_fetch(
            fetch_buffer, &fetched, &vad_active, &wakeup);
        if (ESP_OK != fetch_result || 0U == fetched) {
            vTaskDelay(pdMS_TO_TICKS(5U));
            continue;
        }
        capture_afe_samples(fetch_buffer, fetched);
        if (wakeup) {
            capture_wakeup();
            ESP_LOGI(TAG, "[fetch] WAKENET_DETECTED window=%d",
                     (int)s_capture_window);
        }
    }

    heap_caps_free(fetch_buffer);
    ESP_LOGI(TAG, "[fetch] exited");
    signal_task_exit(AEC_TEST_TASK_FETCH);
    vTaskDelete(NULL);
}

static bool mark_lifecycle_tasks_joined(aec_test_lifecycle_t *lifecycle)
{
    if (NULL == lifecycle) {
        return false;
    }
    (void)aec_test_lifecycle_mark_task_exited(lifecycle, AEC_TEST_TASK_FEED);
    (void)aec_test_lifecycle_mark_task_exited(lifecycle, AEC_TEST_TASK_FETCH);
    return aec_test_lifecycle_mark_task_exited(lifecycle, AEC_TEST_TASK_TONE);
}

static pass_run_status_t run_pass(const aec_test_pass_plan_t *plan,
                                  pass_capture_t *capture)
{
    if (NULL == plan || NULL == capture) {
        return PASS_RUN_FAILED_CLEAN;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "====== Pass %c: AEC %s ======",
             (AEC_TEST_PASS_AEC_ON == plan->id) ? 'A' : 'B',
             plan->aec_enabled ? "ON" : "OFF");

    const audio_processor_config_t afe_config = {
        .mic_channels = 2U,
        .ref_channels = 1U,
        .enable_ns = true,
        .enable_aec = plan->aec_enabled,
        .enable_vad = false,
        .enable_wakenet = plan->wakenet_enabled,
        .wakenet_threshold = plan->wakenet_threshold,
        .aec_mode = AEC_MODE_VOIP_LOW_COST,
    };
    if (ESP_OK != audio_processor_init(&afe_config)) {
        ESP_LOGE(TAG, "AFE init failed for AEC %s",
                 plan->aec_enabled ? "ON" : "OFF");
        return PASS_RUN_FAILED_CLEAN;
    }

    s_feed_chunk = audio_processor_get_feed_chunksize();
    s_fetch_chunk = audio_processor_get_fetch_chunksize();
    if (0U == s_feed_chunk || 0U == s_fetch_chunk) {
        ESP_LOGE(TAG, "invalid AFE chunk sizes feed=%u fetch=%u",
                 (unsigned)s_feed_chunk, (unsigned)s_fetch_chunk);
        audio_processor_deinit();
        return PASS_RUN_FAILED_CLEAN;
    }

    s_task_done_sem = xSemaphoreCreateCounting(3U, 0U);
    if (NULL == s_task_done_sem) {
        ESP_LOGE(TAG, "task completion semaphore allocation failed");
        audio_processor_deinit();
        return PASS_RUN_FAILED_CLEAN;
    }

    taskENTER_CRITICAL(&s_task_exit_lock);
    s_task_exit_mask = 0U;
    taskEXIT_CRITICAL(&s_task_exit_lock);
    s_task_failed = false;
    s_capture_window = CAPTURE_WINDOW_NONE;
    s_active_capture = capture;
    s_capture_active = true;
    s_tone_active = false;

    aec_test_lifecycle_t lifecycle;
    aec_test_lifecycle_begin(&lifecycle);
    uint8_t created_mask = 0U;
    bool sequence_ok = true;

    play_beeps(PASS_START_BEEP_COUNT);
    if (pdPASS == xTaskCreate(fetch_task, "aec_fetch", 8192U, NULL,
                              tskIDLE_PRIORITY + 3U, NULL)) {
        created_mask |= AEC_TEST_TASK_FETCH;
    } else {
        ESP_LOGE(TAG, "fetch task create failed");
        sequence_ok = false;
    }

    if (sequence_ok) {
        vTaskDelay(pdMS_TO_TICKS(100U));
        if (pdPASS == xTaskCreate(feed_task, "aec_feed", 8192U, NULL,
                                  tskIDLE_PRIORITY + 2U, NULL)) {
            created_mask |= AEC_TEST_TASK_FEED;
        } else {
            ESP_LOGE(TAG, "feed task create failed");
            sequence_ok = false;
        }
    }

    if (sequence_ok) {
        vTaskDelay(pdMS_TO_TICKS(TASK_WARMUP_MS));
        amp_enable();
        esp_codec_dev_set_out_vol(s_out_dev, plan->output_volume);
        s_capture_window = CAPTURE_WINDOW_TONE;
        s_tone_active = true;
        if (pdPASS == xTaskCreate(tone_task, "aec_tone", 4096U, (void *)plan,
                                  tskIDLE_PRIORITY + 2U, NULL)) {
            created_mask |= AEC_TEST_TASK_TONE;
            ESP_LOGI(TAG,
                     ">>> tone window %lums: repeat wake word for playback detection <<<",
                     (unsigned long)plan->tone_duration_ms);
            vTaskDelay(pdMS_TO_TICKS(plan->tone_duration_ms));
            s_tone_active = false;
            s_capture_window = CAPTURE_WINDOW_NONE;
            if (!wait_for_task_mask(AEC_TEST_TASK_TONE,
                                    TASK_JOIN_TIMEOUT_MS)) {
                ESP_LOGE(TAG, "tone task stop timeout");
                sequence_ok = false;
            }
            amp_disable();
        } else {
            ESP_LOGE(TAG, "tone task create failed");
            s_tone_active = false;
            s_capture_window = CAPTURE_WINDOW_NONE;
            amp_disable();
            sequence_ok = false;
        }
    }

    if (sequence_ok) {
        s_capture_window = CAPTURE_WINDOW_RECOVERY;
        ESP_LOGI(TAG,
                 ">>> recovery window %lums: speak and repeat wake word <<<",
                 (unsigned long)plan->recovery_duration_ms);
        vTaskDelay(pdMS_TO_TICKS(plan->recovery_duration_ms));
        s_capture_window = CAPTURE_WINDOW_NONE;
    }

    s_tone_active = false;
    s_capture_window = CAPTURE_WINDOW_NONE;
    s_capture_active = false;
    if (!aec_test_lifecycle_request_stop(&lifecycle)) {
        sequence_ok = false;
    }

    if (!wait_for_task_mask(created_mask, TASK_JOIN_TIMEOUT_MS)) {
        ESP_LOGE(TAG,
                 "task join timeout created=0x%02x exited=0x%02x; AFE retained",
                 created_mask, get_task_exit_mask());
        amp_disable();
        aec_test_lifecycle_fail(&lifecycle);
        return PASS_RUN_FAILED_UNSAFE;
    }

    const uint8_t exited_mask = get_task_exit_mask();
    if (TASK_MASK_ALL != created_mask || TASK_MASK_ALL != exited_mask ||
        !mark_lifecycle_tasks_joined(&lifecycle)) {
        sequence_ok = false;
    }
    if (s_task_failed) {
        sequence_ok = false;
    }

    s_active_capture = NULL;
    audio_processor_deinit();
    if (sequence_ok) {
        if (!aec_test_lifecycle_destroy_afe(&lifecycle)) {
            sequence_ok = false;
        }
    } else {
        aec_test_lifecycle_fail(&lifecycle);
    }
    vSemaphoreDelete(s_task_done_sem);
    s_task_done_sem = NULL;
    amp_disable();

    ESP_LOGI(TAG,
             "[capture] feed tone=%u recovery=%u afe tone=%u recovery=%u wake=%lu/%lu",
             (unsigned)capture->feed_tone_written,
             (unsigned)capture->feed_recovery_written,
             (unsigned)capture->afe_tone_written,
             (unsigned)capture->afe_recovery_written,
             (unsigned long)capture->wake_tone_count,
             (unsigned long)capture->wake_recovery_count);
    return sequence_ok ? PASS_RUN_OK : PASS_RUN_FAILED_CLEAN;
}

static bool analyze_segment(const int16_t *samples, size_t sample_count,
                            bool measure_tone, uint32_t tone_hz,
                            aec_test_signal_metrics_t *metrics)
{
    if (NULL == samples || NULL == metrics || 0U == sample_count) {
        return false;
    }

    aec_test_signal_accumulator_t accumulator;
    aec_test_signal_reset(&accumulator);
    for (size_t i = 0U; i < sample_count; i++) {
        int16_t in_phase = 0;
        int16_t quadrature = 0;
        if (measure_tone) {
            const uint32_t phase_index = (uint32_t)(i % SAMPLE_RATE);
            const float phase = TWO_PI_F * (float)tone_hz *
                                (float)phase_index / (float)SAMPLE_RATE;
            in_phase = (int16_t)(TONE_REFERENCE_AMPLITUDE * sinf(phase));
            quadrature = (int16_t)(TONE_REFERENCE_AMPLITUDE * cosf(phase));
        }
        aec_test_signal_add(&accumulator, samples[i], in_phase, quadrature);
    }
    return aec_test_signal_summarize(&accumulator, metrics);
}

static bool analyze_pass(const aec_test_pass_plan_t *plan,
                         const pass_capture_t *capture,
                         pass_metrics_t *metrics)
{
    if (NULL == plan || NULL == capture || NULL == metrics) {
        return false;
    }
    *metrics = (pass_metrics_t){0};
    const size_t recovery_offset = capture->tone_capacity;
    return analyze_segment(capture->mic, capture->feed_tone_written, true,
                           plan->tone_hz, &metrics->mic_tone) &&
           analyze_segment(capture->ref, capture->feed_tone_written, true,
                           plan->tone_hz, &metrics->ref_tone) &&
           analyze_segment(capture->afe, capture->afe_tone_written, true,
                           plan->tone_hz, &metrics->afe_tone) &&
           analyze_segment(capture->mic + recovery_offset,
                           capture->feed_recovery_written, true,
                           plan->tone_hz, &metrics->mic_recovery) &&
           analyze_segment(capture->ref + recovery_offset,
                           capture->feed_recovery_written, true,
                           plan->tone_hz, &metrics->ref_recovery) &&
           analyze_segment(capture->afe + recovery_offset,
                           capture->afe_recovery_written, true,
                           plan->tone_hz, &metrics->afe_recovery);
}

static void log_signal_metrics(const char *pass_name, const char *window,
                               const char *signal,
                               const aec_test_signal_metrics_t *metrics,
                               bool include_tone)
{
    if (NULL == pass_name || NULL == window || NULL == signal || NULL == metrics) {
        return;
    }
    if (include_tone) {
        ESP_LOGI(TAG,
                 "[%s][%s][%s] n=%lu rms=%.1f peak=%ld clip=%.3f%% tone1k_rms=%.2f",
                 pass_name, window, signal,
                 (unsigned long)metrics->sample_count, (double)metrics->rms,
                 (long)metrics->peak,
                 (double)(metrics->clipping_ratio * METRIC_CLIP_PERCENT_SCALE),
                 (double)metrics->tone_rms);
    } else {
        ESP_LOGI(TAG, "[%s][%s][%s] n=%lu rms=%.1f peak=%ld clip=%.3f%%",
                 pass_name, window, signal,
                 (unsigned long)metrics->sample_count, (double)metrics->rms,
                 (long)metrics->peak,
                 (double)(metrics->clipping_ratio * METRIC_CLIP_PERCENT_SCALE));
    }
}

static void log_pass_metrics(const char *pass_name,
                             const pass_capture_t *capture,
                             const pass_metrics_t *metrics)
{
    log_signal_metrics(pass_name, "tone", "MIC", &metrics->mic_tone, true);
    log_signal_metrics(pass_name, "tone", "REF", &metrics->ref_tone, true);
    log_signal_metrics(pass_name, "tone", "AFE", &metrics->afe_tone, true);
    log_signal_metrics(pass_name, "recovery", "MIC", &metrics->mic_recovery, true);
    log_signal_metrics(pass_name, "recovery", "REF", &metrics->ref_recovery, true);
    log_signal_metrics(pass_name, "recovery", "AFE", &metrics->afe_recovery, true);
    ESP_LOGI(TAG, "[%s][WakeNet] tone=%lu recovery=%lu",
             pass_name, (unsigned long)capture->wake_tone_count,
             (unsigned long)capture->wake_recovery_count);
}

static int32_t adaptive_playback_gain(const aec_test_signal_metrics_t *metrics)
{
    if (NULL == metrics || metrics->peak <= 0) {
        return 1;
    }
    int32_t gain = PLAYBACK_TARGET_PEAK / metrics->peak;
    if (gain > PLAYBACK_GAIN_MAX) {
        gain = PLAYBACK_GAIN_MAX;
    } else if (gain < 1) {
        gain = 1;
    }
    return gain;
}

static void play_scaled_segment(const int16_t *samples, size_t sample_count,
                                int32_t gain)
{
    if (NULL == samples || 0U == sample_count) {
        return;
    }
    int16_t output[PLAYBACK_CHUNK_SAMPLES];
    size_t offset = 0U;
    while (offset < sample_count) {
        size_t chunk = sample_count - offset;
        if (chunk > PLAYBACK_CHUNK_SAMPLES) {
            chunk = PLAYBACK_CHUNK_SAMPLES;
        }
        for (size_t i = 0U; i < chunk; i++) {
            int32_t scaled = (int32_t)samples[offset + i] * gain;
            if (scaled > INT16_MAX) {
                scaled = INT16_MAX;
            } else if (scaled < INT16_MIN) {
                scaled = INT16_MIN;
            }
            output[i] = (int16_t)scaled;
        }
        if (ESP_OK != esp_codec_dev_write(s_out_dev, output,
                                          chunk * sizeof(int16_t))) {
            ESP_LOGE(TAG, "comparison playback write failed");
            break;
        }
        offset += chunk;
    }
}

static void play_recovery_comparison(const pass_capture_t captures[AEC_TEST_PASS_COUNT],
                                     const pass_metrics_t metrics[AEC_TEST_PASS_COUNT])
{
    ESP_LOGI(TAG, "========== RECOVERY PLAYBACK COMPARISON ==========");
    for (size_t pass = 0U; pass < AEC_TEST_PASS_COUNT; pass++) {
        const int32_t gain = adaptive_playback_gain(&metrics[pass].afe_recovery);
        play_beeps((0U == pass) ? 3 : 4);
        ESP_LOGI(TAG, ">>> Playing Pass %c recovery AFE output, gain=%ld <<<",
                 (0U == pass) ? 'A' : 'B', (long)gain);
        amp_enable();
        esp_codec_dev_set_out_vol(s_out_dev, OUTPUT_VOL);
        play_scaled_segment(captures[pass].afe + captures[pass].tone_capacity,
                            captures[pass].afe_recovery_written, gain);
        amp_disable();
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static bool configure_mic_pga(void)
{
    const esp_codec_dev_handle_t input_dev =
        board_laiwfs300_audio_get_input_dev();
    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    const i2c_device_config_t es7210_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x40,
        .scl_speed_hz = 100000U,
    };
    i2c_master_dev_handle_t es7210_device = NULL;
    const esp_err_t add_result =
        i2c_master_bus_add_device(bus, &es7210_config, &es7210_device);
    if (ESP_OK != add_result) {
        ESP_LOGW(TAG, "ES7210 direct I2C add failed; using codec gain API");
        return ESP_OK == esp_codec_dev_set_in_channel_gain(
            input_dev,
            ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
            37.5f);
    }

    const uint8_t register_17[2] = {0x17, 0x60};
    const uint8_t register_18[2] = {0x18, 0x60};
    uint8_t value = 0U;
    bool success = ESP_OK == i2c_master_transmit(es7210_device,
                                                  register_17, 2U, 100) &&
                   ESP_OK == i2c_master_transmit(es7210_device,
                                                  register_18, 2U, 100) &&
                   ESP_OK == i2c_master_transmit_receive(
                       es7210_device, register_17, 1U, &value, 1U, 100);
    i2c_master_bus_rm_device(es7210_device);
    if (success) {
        ESP_LOGI(TAG, "ES7210 REG17=0x%02x REG18=0x60 (48dB)", value);
    } else {
        ESP_LOGE(TAG, "ES7210 PGA register configuration failed");
    }
    return success;
}

static void run_test(void)
{
    const aec_test_plan_config_t plan_config = {
        .tone_duration_ms = TONE_DURATION_MS,
        .recovery_duration_ms = RECOVERY_DURATION_MS,
        .tone_hz = TEST_TONE_HZ,
        .tone_amplitude = TONE_AMPLITUDE,
        .output_volume = OUTPUT_VOL,
        .wakenet_enabled = true,
        .wakenet_threshold = WAKENET_THRESHOLD,
    };
    aec_test_pass_plan_t plans[AEC_TEST_PASS_COUNT];
    pass_capture_t captures[AEC_TEST_PASS_COUNT] = {0};
    pass_metrics_t metrics[AEC_TEST_PASS_COUNT] = {0};

    if (!aec_test_build_pass_plan(&plan_config, plans)) {
        ESP_LOGE(TAG, "failed to build controlled A/B plan");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Controlled AEC ON/OFF + WakeNet test");
    ESP_LOGI(TAG, " tone=%luHz amp=%d tone=%lums recovery=%lums",
             (unsigned long)TEST_TONE_HZ, TONE_AMPLITUDE,
             (unsigned long)TONE_DURATION_MS,
             (unsigned long)RECOVERY_DURATION_MS);
    ESP_LOGI(TAG,
             " MMR slot0=MIC1 slot1=REF slot2=MIC2 REF_GAIN=%d VOL=%u PGA=%.1fdB",
             REF_GAIN, OUTPUT_VOL, (double)MIC_PGA_DB);
    ESP_LOGI(TAG, " WakeNet threshold=%.2f; AEC mode=%d; NS=NSNet2",
             (double)WAKENET_THRESHOLD, AEC_MODE_VOIP_LOW_COST);
    ESP_LOGI(TAG,
             " Reference AFE_MODE_LOW_COST + MR + SE/NS-off remains a separate candidate; this run uses the existing audio_processor interface");
    ESP_LOGI(TAG, "========================================");

    if (ESP_OK != board_laiwfs300_audio_init()) {
        ESP_LOGE(TAG, "audio initialization failed");
        return;
    }
    s_out_dev = board_laiwfs300_audio_get_output_dev();
    if (NULL == s_out_dev || !configure_mic_pga()) {
        ESP_LOGE(TAG, "audio codec setup failed");
        return;
    }
    if (ESP_OK != board_laiwfs300_audio_open_input_all_channels()) {
        ESP_LOGE(TAG, "open_input_all_channels failed");
        return;
    }

    const size_t tone_samples = (SAMPLE_RATE * TONE_DURATION_MS) / 1000U;
    const size_t recovery_samples = (SAMPLE_RATE * RECOVERY_DURATION_MS) / 1000U;
    for (size_t pass = 0U; pass < AEC_TEST_PASS_COUNT; pass++) {
        if (!pass_capture_alloc(&captures[pass], tone_samples, recovery_samples)) {
            ESP_LOGE(TAG, "PSRAM capture allocation failed for pass %u",
                     (unsigned)pass);
            for (size_t cleanup = 0U; cleanup < pass; cleanup++) {
                pass_capture_free(&captures[cleanup]);
            }
            return;
        }
    }

    bool safe_to_free = true;
    for (size_t pass = 0U; pass < AEC_TEST_PASS_COUNT; pass++) {
        const pass_run_status_t status = run_pass(&plans[pass], &captures[pass]);
        if (PASS_RUN_OK != status) {
            ESP_LOGE(TAG, "Pass %c failed status=%d",
                     (0U == pass) ? 'A' : 'B', (int)status);
            safe_to_free = PASS_RUN_FAILED_UNSAFE != status;
            break;
        }
        if (!analyze_pass(&plans[pass], &captures[pass], &metrics[pass])) {
            ESP_LOGE(TAG, "Pass %c has incomplete capture data",
                     (0U == pass) ? 'A' : 'B');
            break;
        }
        log_pass_metrics((0U == pass) ? "AEC_ON" : "AEC_OFF",
                         &captures[pass], &metrics[pass]);
        if ((pass + 1U) < AEC_TEST_PASS_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(PASS_GAP_MS));
        }
    }

    if (safe_to_free && 0U != metrics[AEC_TEST_PASS_AEC_ON].afe_tone.sample_count &&
        0U != metrics[AEC_TEST_PASS_AEC_OFF].afe_tone.sample_count) {
        float suppression_db = 0.0f;
        if (aec_test_compute_suppression_db(
                metrics[AEC_TEST_PASS_AEC_OFF].afe_tone.tone_rms,
                metrics[AEC_TEST_PASS_AEC_ON].afe_tone.tone_rms,
                &suppression_db)) {
            ESP_LOGI(TAG, "[A/B] AFE 1kHz suppression OFF/ON = %.2f dB",
                     (double)suppression_db);
        }
        ESP_LOGI(TAG,
                 "[A/B] playback WakeNet detections ON=%lu OFF=%lu; application TTS wake-interrupt remains final acceptance",
                 (unsigned long)captures[AEC_TEST_PASS_AEC_ON].wake_tone_count,
                 (unsigned long)captures[AEC_TEST_PASS_AEC_OFF].wake_tone_count);
        play_recovery_comparison(captures, metrics);
    }

    if (!safe_to_free) {
        ESP_LOGE(TAG,
                 "resources retained because a task did not join; reboot before another test");
        return;
    }
    for (size_t pass = 0U; pass < AEC_TEST_PASS_COUNT; pass++) {
        pass_capture_free(&captures[pass]);
    }
    ESP_LOGI(TAG, "========== TEST DONE ==========");
}

static void test_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "waiting %lus for serial...",
             (unsigned long)(STARTUP_DELAY_MS / 1000U));
    vTaskDelay(pdMS_TO_TICKS(STARTUP_DELAY_MS));
    run_test();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Controlled AEC + WakeNet Evaluation ===");
    const esp_err_t init_result = board_laiwfs300_init();
    if (ESP_OK != init_result) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(init_result));
        return;
    }
    if (pdPASS != xTaskCreate(test_task, "aec_test", 8192U, NULL,
                              tskIDLE_PRIORITY + 4U, NULL)) {
        ESP_LOGE(TAG, "test task create failed");
    }
}
