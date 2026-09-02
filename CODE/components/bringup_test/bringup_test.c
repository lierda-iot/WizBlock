#include "bringup_test.h"

#include "aip8563_rtc.h"
#include "bmi260_imu.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "camera_hal.h"
#include "display_hal.h"
#include "io_expander.h"
#include "lte_hal.h"
#include "pt2466_motor.h"
#include "robot_motion.h"
#include "touch_hal.h"

#if CONFIG_LAIWFS300_ENABLE_AUDIO_NS_TEST
#include "audio_processor.h"
#endif

#if CONFIG_LAIWFS300_ENABLE_LTE_NET_TEST
#include "esp_event.h"
#include "esp_wifi.h"
#include "lsd_net_mgmt.h"
#include "nvs_flash.h"
#endif

#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

static const char *TAG = "bringup_test";

#if CONFIG_LAIWFS300_ENABLE_MOTOR_SMOKE_TEST && CONFIG_LAIWFS300_MOTOR_SMOKE_TEST_DRY_RUN
typedef struct {
    const char *name;
} dry_run_motor_io_t;

static esp_err_t dry_run_set_input(void *ctx, uint8_t input_index, uint16_t duty_permille)
{
    const dry_run_motor_io_t *io = (const dry_run_motor_io_t *)ctx;
    ESP_LOGI(TAG, "motor dry-run: %s input=%u duty=%u/1000", io->name, input_index, duty_permille);
    return ESP_OK;
}
#endif

#if CONFIG_LAIWFS300_BRINGUP_TEST_LOG_TASK
static void bringup_log_task(void *arg)
{
    (void)arg;
    uint32_t tick = 0;

    while (true) {
        ESP_LOGI(TAG,
                 "heartbeat=%lu motor_smoke=%s dry_run=%s serial=UART0/115200/8N1",
                 (unsigned long)tick++,
#if CONFIG_LAIWFS300_ENABLE_MOTOR_SMOKE_TEST
                 "enabled",
#else
                 "disabled",
#endif
#if CONFIG_LAIWFS300_ENABLE_MOTOR_SMOKE_TEST && CONFIG_LAIWFS300_MOTOR_SMOKE_TEST_DRY_RUN
                 "enabled"
#elif CONFIG_LAIWFS300_ENABLE_MOTOR_SMOKE_TEST
                 "disabled"
#else
                 "not_applicable"
#endif
        );
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LAIWFS300_BRINGUP_TEST_LOG_PERIOD_MS));
    }
}
#endif

#if CONFIG_LAIWFS300_ENABLE_DISPLAY_COLOR_TEST
static void display_color_task(void *arg)
{
    (void)arg;
    bool red = false;

    while (true) {
        uint16_t color = red ? DISPLAY_HAL_RGB565_RED : DISPLAY_HAL_RGB565_WHITE;
        ESP_LOGI(TAG, "display color test: %s", red ? "red" : "white");
        esp_err_t ret = board_laiwfs300_display_fill_rgb565(color);
        if (ESP_OK != ret) {
            ESP_LOGE(TAG, "display color test failed: %s", esp_err_to_name(ret));
        }
        red = !red;
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LAIWFS300_DISPLAY_COLOR_TEST_PERIOD_MS));
    }
}
#endif

static esp_err_t start_display_color_test_if_enabled(void)
{
#if CONFIG_LAIWFS300_ENABLE_DISPLAY_COLOR_TEST
    ESP_LOGW(TAG, "display white/red color test enabled");
    ESP_RETURN_ON_ERROR(board_laiwfs300_display_init(), TAG, "display init failed");
    ESP_RETURN_ON_ERROR(board_laiwfs300_display_fill_rgb565(DISPLAY_HAL_RGB565_WHITE),
                        TAG,
                        "initial white fill failed");
    BaseType_t created = xTaskCreate(display_color_task,
                                     "display_color",
                                     4096,
                                     NULL,
                                     tskIDLE_PRIORITY + 1,
                                     NULL);
    return (pdPASS == created) ? ESP_OK : ESP_ERR_NO_MEM;
#else
    ESP_LOGI(TAG, "display color test disabled by Kconfig");
    return ESP_OK;
#endif
}

#if CONFIG_LAIWFS300_ENABLE_CAMERA_PREVIEW_TEST
#define CAMERA_PREVIEW_PERIOD_MS 10000
#define CAMERA_PREVIEW_LCD_W 240
#define CAMERA_PREVIEW_LCD_H 320

static void camera_preview_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "camera preview: initializing camera");
    esp_err_t ret = board_laiwfs300_camera_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "camera preview: init failed: %s, task exiting", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "camera preview: initializing display");
    ret = board_laiwfs300_display_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "camera preview: display init failed: %s, task exiting", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    uint16_t *rgb_buf = heap_caps_malloc(CAMERA_PREVIEW_LCD_W * CAMERA_PREVIEW_LCD_H * sizeof(uint16_t),
                                          MALLOC_CAP_SPIRAM);
    if (NULL == rgb_buf) {
        ESP_LOGE(TAG, "camera preview: RGB buffer alloc failed, task exiting");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "camera preview: running, period=%d ms", CAMERA_PREVIEW_PERIOD_MS);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CAMERA_PREVIEW_PERIOD_MS));

        int64_t t0 = esp_timer_get_time();
        uint8_t *frame = NULL;
        size_t frame_len = 0;

        ret = board_laiwfs300_camera_capture(&frame, &frame_len);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "camera preview: capture failed: %s", esp_err_to_name(ret));
            continue;
        }

        int64_t t1 = esp_timer_get_time();
        ESP_LOGI(TAG, "camera preview: captured %u bytes in %lld ms",
                 (unsigned)frame_len, (long long)(t1 - t0) / 1000);

        ret = camera_hal_yuv422_crop_to_rgb565(frame, 640, 480,
                                                rgb_buf, CAMERA_PREVIEW_LCD_W, CAMERA_PREVIEW_LCD_H);
        board_laiwfs300_camera_release_frame(frame);

        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "camera preview: YUV->RGB conversion failed: %s", esp_err_to_name(ret));
            continue;
        }

        int64_t t2 = esp_timer_get_time();
        ret = board_laiwfs300_display_draw_bitmap_rgb565(0, 0, CAMERA_PREVIEW_LCD_W, CAMERA_PREVIEW_LCD_H, rgb_buf);
        int64_t t3 = esp_timer_get_time();

        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "camera preview: display draw failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "camera preview: convert=%lld ms draw=%lld ms total=%lld ms",
                     (long long)(t2 - t1) / 1000, (long long)(t3 - t2) / 1000,
                     (long long)(t3 - t0) / 1000);
        }
    }
}
#endif

static esp_err_t start_camera_preview_if_enabled(void)
{
#if CONFIG_LAIWFS300_ENABLE_CAMERA_PREVIEW_TEST
    ESP_LOGW(TAG, "camera preview test enabled (10s interval)");
    BaseType_t created = xTaskCreate(camera_preview_task,
                                     "cam_preview",
                                     8192,
                                     NULL,
                                     tskIDLE_PRIORITY + 5,
                                     NULL);
    return (pdPASS == created) ? ESP_OK : ESP_ERR_NO_MEM;
#else
    ESP_LOGI(TAG, "camera preview test disabled by Kconfig");
    return ESP_OK;
#endif
}

#if CONFIG_LAIWFS300_ENABLE_TOUCH_TEST
static void touch_test_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "touch test: initializing FT6206");
    esp_err_t ret = board_laiwfs300_touch_init();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "touch test: init failed (%s), task exiting", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = board_laiwfs300_touch_verify();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "touch test: verify failed (%s), task exiting", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "touch test: polling touch events (period=%d ms)", CONFIG_LAIWFS300_TOUCH_TEST_POLL_MS);
    uint32_t poll_count = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LAIWFS300_TOUCH_TEST_POLL_MS));
        touch_panel_point_t point = {0};
        uint8_t count = 0;
        ret = touch_panel_read_point(&point, &count);
        if (ESP_OK == ret && count > 0) {
            ESP_LOGI(TAG, "TOUCH: x=%u y=%u weight=%u event=%u count=%u",
                     point.x, point.y, point.weight, point.event, count);
        }
        poll_count++;
        if (0 == (poll_count % 60)) {
            ESP_LOGI(TAG, "touch test: %lu polls, awaiting touch...", (unsigned long)poll_count);
        }
    }
}
#endif

static esp_err_t start_touch_test_if_enabled(void)
{
#if CONFIG_LAIWFS300_ENABLE_TOUCH_TEST
    ESP_LOGW(TAG, "touch IC bring-up test enabled");
    BaseType_t created = xTaskCreate(touch_test_task,
                                     "touch_test",
                                     4096,
                                     NULL,
                                     tskIDLE_PRIORITY + 2,
                                     NULL);
    return (pdPASS == created) ? ESP_OK : ESP_ERR_NO_MEM;
#else
    ESP_LOGI(TAG, "touch test disabled by Kconfig");
    return ESP_OK;
#endif
}

static esp_err_t run_motor_smoke_test_if_enabled(void)
{
#if CONFIG_LAIWFS300_ENABLE_MOTOR_SMOKE_TEST
    ESP_LOGW(TAG, "motor smoke test enabled");
#if CONFIG_LAIWFS300_MOTOR_SMOKE_TEST_DRY_RUN
    ESP_LOGW(TAG, "motor smoke test (dry-run): forward 100%%");
    static dry_run_motor_io_t left_io = {
        .name = "left_track",
    };
    static dry_run_motor_io_t right_io = {
        .name = "right_track",
    };
    static pt2466_motor_t left_motor;
    static pt2466_motor_t right_motor;
    static robot_motion_t motion;

    const pt2466_motor_config_t left_config = {
        .name = "left_track",
        .positive_input = 1,
        .negative_input = 2,
        .invert_direction = false,
        .set_input = dry_run_set_input,
        .user_ctx = &left_io,
    };
    const pt2466_motor_config_t right_config = {
        .name = "right_track",
        .positive_input = 3,
        .negative_input = 4,
        .invert_direction = false,
        .set_input = dry_run_set_input,
        .user_ctx = &right_io,
    };

    ESP_RETURN_ON_ERROR(pt2466_motor_init(&left_motor, &left_config), TAG, "left motor init failed");
    ESP_RETURN_ON_ERROR(pt2466_motor_init(&right_motor, &right_config), TAG, "right motor init failed");
    ESP_RETURN_ON_ERROR(robot_motion_init(&motion, &left_motor, &right_motor), TAG, "motion init failed");
    ESP_RETURN_ON_ERROR(robot_motion_forward(&motion, 100), TAG, "forward failed");
    ESP_LOGW(TAG, "motor dry-run: forward 100%% indefinitely");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "real motor: forward 100%%");
    ESP_RETURN_ON_ERROR(board_laiwfs300_motor_init(), TAG, "board motor init failed");
    robot_motion_t *motion = board_laiwfs300_motion();
    if (NULL == motion) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(robot_motion_forward(motion, 100), TAG, "forward failed");
    board_laiwfs300_motor_dump_state();
    ESP_LOGW(TAG, "motor running forward 100%% indefinitely");
    return ESP_OK;
#endif
#else
    ESP_LOGI(TAG, "motor smoke test disabled by Kconfig");
    return ESP_OK;
#endif
}

static void run_rtc_test(void)
{
#if CONFIG_LAIWFS300_ENABLE_RTC_TEST
    ESP_LOGI(TAG, "RTC test: initializing AIP8563");
    if (ESP_OK != aip8563_init()) {
        ESP_LOGW(TAG, "RTC test: init failed");
        return;
    }
    aip8563_time_t t = { .year = 26, .month = 6, .day = 15, .hours = 12, .minutes = 0, .seconds = 0 };
    aip8563_set_time(&t);
    vTaskDelay(pdMS_TO_TICKS(3000));
    aip8563_time_t t2 = {0};
    aip8563_get_time(&t2);
    ESP_LOGI(TAG, "RTC test: set 12:00:00, read back %02u:%02u:%02u (expect ~12:00:03)",
             t2.hours, t2.minutes, t2.seconds);
    if (aip8563_power_lost()) {
        ESP_LOGW(TAG, "RTC: VL flag set (power was lost)");
    }
#endif
}

static void run_imu_test(void)
{
#if CONFIG_LAIWFS300_ENABLE_IMU_TEST
    ESP_LOGI(TAG, "IMU test: initializing BMI260");
    if (ESP_OK != bmi260_init()) {
        ESP_LOGW(TAG, "IMU test: init failed");
        return;
    }
    ESP_LOGI(TAG, "IMU test: BMI260 initialized, continuous reading every 1s");
#endif
}

#if CONFIG_LAIWFS300_ENABLE_IMU_TEST
static void imu_continuous_task(void *arg)
{
    (void)arg;
    while (true) {
        bmi260_raw_data_t acc = {0}, gyr = {0};
        bmi260_read_accel(&acc);
        bmi260_read_gyro(&gyr);
        ESP_LOGI(TAG, "IMU: acc x=%d y=%d z=%d | gyr x=%d y=%d z=%d",
                 acc.x, acc.y, acc.z, gyr.x, gyr.y, gyr.z);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

static void run_audio_test(void)
{
#if CONFIG_LAIWFS300_ENABLE_AUDIO_NS_TEST
    ESP_LOGI(TAG, "Audio NS test: waiting 6s for serial connection...");
    vTaskDelay(pdMS_TO_TICKS(6000));
    ESP_LOGI(TAG, "Audio NS test: initializing I2S + codecs");
    esp_err_t ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Audio NS test: audio init FAILED: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    audio_processor_config_t afe_cfg = {
        .mic_channels = 1,
        .ref_channels = 0,
        .enable_ns = true,
        .enable_aec = false,
        .enable_vad = true,
    };
    ret = audio_processor_init(&afe_cfg);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Audio NS test: AFE init FAILED: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    size_t feed_chunk = audio_processor_get_feed_chunksize();
    size_t fetch_chunk = audio_processor_get_fetch_chunksize();
    ESP_LOGI(TAG, "Audio NS test: AFE ready, feed=%u fetch=%u", (unsigned)feed_chunk, (unsigned)fetch_chunk);

    const uint32_t sample_rate = BOARD_LAIWFS300_I2S_SAMPLE_RATE;
    const uint32_t record_seconds = 10;
    const uint32_t total_samples = sample_rate * record_seconds;
    const uint32_t total_bytes = total_samples * sizeof(int16_t);

    int16_t *raw_buf = heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM);
    int16_t *ns_buf = heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM);
    int16_t *feed_buf = heap_caps_malloc(feed_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *fetch_buf = heap_caps_malloc(fetch_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (NULL == raw_buf || NULL == ns_buf || NULL == feed_buf || NULL == fetch_buf) {
        ESP_LOGE(TAG, "Audio NS test: PSRAM alloc failed");
        vTaskDelete(NULL);
        return;
    }

    esp_codec_dev_handle_t out_dev = board_laiwfs300_audio_get_output_dev();
    io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    esp_codec_dev_set_out_vol(out_dev, 100);

    /* Beep -> Record */
    {
        int16_t tone[128];
        uint32_t written = 0;
        uint32_t beep_samples = sample_rate * 200 / 1000;
        while (written < beep_samples) {
            size_t n = (beep_samples - written < 128) ? (beep_samples - written) : 128;
            for (size_t i = 0; i < n; i++) {
                float t = (float)(written + i) / (float)sample_rate;
                tone[i] = (int16_t)(12000.0f * sinf(2.0f * 3.14159265f * 1000.0f * t));
            }
            esp_codec_dev_write(out_dev, tone, n * sizeof(int16_t));
            written += n;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Audio NS test: RECORDING 10s...");
    size_t raw_offset = 0;
    size_t ns_offset = 0;

    const size_t feeds_per_fetch = (fetch_chunk + feed_chunk - 1) / feed_chunk;

    while (raw_offset < total_samples) {
        for (size_t f = 0; f < feeds_per_fetch && raw_offset < total_samples; f++) {
            ret = board_laiwfs300_audio_read_raw(feed_buf, feed_chunk, 1);
            if (ESP_OK != ret) { goto recording_done; }
            size_t copy_n = feed_chunk;
            if (raw_offset + copy_n > total_samples) {
                copy_n = total_samples - raw_offset;
            }
            memcpy(&raw_buf[raw_offset], feed_buf, copy_n * sizeof(int16_t));
            raw_offset += copy_n;
            audio_processor_feed(feed_buf, feed_chunk);
        }

        size_t fetched = 0;
        bool vad = false;
        if (ESP_OK == audio_processor_fetch(fetch_buf, &fetched, &vad, NULL)) {
            size_t ns_copy = fetched;
            if (ns_offset + ns_copy > total_samples) {
                ns_copy = total_samples - ns_offset;
            }
            memcpy(&ns_buf[ns_offset], fetch_buf, ns_copy * sizeof(int16_t));
            ns_offset += ns_copy;
        }
    }
recording_done:

    ESP_LOGI(TAG, "Audio NS test: recorded raw=%u ns=%u samples", (unsigned)raw_offset, (unsigned)ns_offset);

    /* RMS comparison */
    int64_t sum_raw = 0, sum_ns = 0;
    for (size_t i = 0; i < raw_offset; i++) {
        sum_raw += (int64_t)raw_buf[i] * raw_buf[i];
    }
    for (size_t i = 0; i < ns_offset; i++) {
        sum_ns += (int64_t)ns_buf[i] * ns_buf[i];
    }
    uint32_t rms_raw = (uint32_t)sqrtf((float)sum_raw / (float)(raw_offset > 0 ? raw_offset : 1));
    uint32_t rms_ns = (uint32_t)sqrtf((float)sum_ns / (float)(ns_offset > 0 ? ns_offset : 1));
    ESP_LOGI(TAG, "Audio NS test: RMS raw=%lu denoised=%lu", (unsigned long)rms_raw, (unsigned long)rms_ns);

    /* Beep -> Play raw */
    {
        int16_t tone[128];
        uint32_t written = 0;
        uint32_t beep_samples = sample_rate * 200 / 1000;
        while (written < beep_samples) {
            size_t n = (beep_samples - written < 128) ? (beep_samples - written) : 128;
            for (size_t i = 0; i < n; i++) {
                float t = (float)(written + i) / (float)sample_rate;
                tone[i] = (int16_t)(12000.0f * sinf(2.0f * 3.14159265f * 1500.0f * t));
            }
            esp_codec_dev_write(out_dev, tone, n * sizeof(int16_t));
            written += n;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "Audio NS test: PLAYING RAW...");
    {
        size_t played = 0;
        while (played < raw_offset) {
            size_t n = (raw_offset - played < 512) ? (raw_offset - played) : 512;
            esp_codec_dev_write(out_dev, &raw_buf[played], n * sizeof(int16_t));
            played += n;
        }
    }

    /* Beep -> Play denoised */
    {
        int16_t tone[128];
        uint32_t written = 0;
        uint32_t beep_samples = sample_rate * 200 / 1000;
        while (written < beep_samples) {
            size_t n = (beep_samples - written < 128) ? (beep_samples - written) : 128;
            for (size_t i = 0; i < n; i++) {
                float t = (float)(written + i) / (float)sample_rate;
                tone[i] = (int16_t)(12000.0f * sinf(2.0f * 3.14159265f * 2000.0f * t));
            }
            esp_codec_dev_write(out_dev, tone, n * sizeof(int16_t));
            written += n;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "Audio NS test: PLAYING DENOISED...");
    {
        size_t played = 0;
        while (played < ns_offset) {
            size_t n = (ns_offset - played < 512) ? (ns_offset - played) : 512;
            esp_codec_dev_write(out_dev, &ns_buf[played], n * sizeof(int16_t));
            played += n;
        }
    }

    ESP_LOGI(TAG, "Audio NS test: DONE");
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT, BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, false);
    heap_caps_free(raw_buf);
    heap_caps_free(ns_buf);
    heap_caps_free(feed_buf);
    heap_caps_free(fetch_buf);
    audio_processor_deinit();

#elif CONFIG_LAIWFS300_ENABLE_AUDIO_LOOPBACK_TEST || CONFIG_LAIWFS300_ENABLE_AUDIO_TONE_TEST || CONFIG_LAIWFS300_ENABLE_MIC_TEST
    ESP_LOGI(TAG, "Audio test: waiting 6s for serial connection...");
    vTaskDelay(pdMS_TO_TICKS(6000));
    ESP_LOGI(TAG, "Audio test: initializing I2S + codecs (esp_codec_dev)");
    esp_err_t ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Audio test: init FAILED: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Audio test: init OK");
#if CONFIG_LAIWFS300_ENABLE_AUDIO_LOOPBACK_TEST
    ESP_LOGI(TAG, "Audio record-play: beep -> record 10s -> beep -> play 10s");
    ret = board_laiwfs300_audio_record_play_start();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Audio record-play: start FAILED: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Audio record-play: STARTED");
    }
#else
#if CONFIG_LAIWFS300_ENABLE_AUDIO_TONE_TEST
    board_laiwfs300_audio_play_tone(1000, 3000);
#endif
#if CONFIG_LAIWFS300_ENABLE_MIC_TEST
    board_laiwfs300_audio_mic_test(2000);
#endif
#endif
#endif
    vTaskDelete(NULL);
}

static void run_ioex_input_test(void)
{
#if CONFIG_LAIWFS300_ENABLE_IOEX_INPUT_TEST
    if (!board_laiwfs300_ioex_available()) {
        ESP_LOGW(TAG, "IOEX input test: IOEX not available");
        return;
    }
    bool tp_int = false, tf_cd = false, rtc_int = false;
    io_expander_read_pin(BOARD_LAIWFS300_IOEX_TP_INT_PORT, BOARD_LAIWFS300_IOEX_TP_INT_PIN, &tp_int);
    io_expander_read_pin(BOARD_LAIWFS300_IOEX_TF_CD_PORT, BOARD_LAIWFS300_IOEX_TF_CD_PIN, &tf_cd);
    io_expander_read_pin(BOARD_LAIWFS300_IOEX_RTC_INT_PORT, BOARD_LAIWFS300_IOEX_RTC_INT_PIN, &rtc_int);
    ESP_LOGI(TAG, "IOEX inputs: TP_INT=%d TF_CD=%d RTC_INT=%d", tp_int, tf_cd, rtc_int);
#endif
}

#if CONFIG_LAIWFS300_ENABLE_LTE_NET_TEST
static int s_wifi_retry_num = 0;
#define LTE_WIFI_MAX_RETRY 10

static void lte_net_switch_cb(lsd_net_if_t new_if)
{
    if (new_if == LSD_IF_WIFI) {
        ESP_LOGI(TAG, "LTE net: switched to Wi-Fi");
    } else if (new_if == LSD_IF_4G) {
        ESP_LOGI(TAG, "LTE net: switched to 4G");
    } else {
        ESP_LOGW(TAG, "LTE net: no network");
    }
}

static void lte_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (WIFI_EVENT == event_base) {
        if (WIFI_EVENT_STA_START == event_id) {
            esp_wifi_connect();
        } else if (WIFI_EVENT_STA_DISCONNECTED == event_id) {
            lsd_net_send_event(NET_WIFI_EVENT_DISCONNECTED);
            if (s_wifi_retry_num < LTE_WIFI_MAX_RETRY) {
                esp_wifi_connect();
                s_wifi_retry_num++;
            } else {
                ESP_LOGW(TAG, "LTE net: WiFi max retries reached");
            }
        }
    } else if (IP_EVENT == event_base && IP_EVENT_STA_GOT_IP == event_id) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "LTE net: WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num = 0;
        lsd_net_send_event(NET_WIFI_EVENT_CONNECTED);
    }
}

static void lte_net_status_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        lsd_net_if_t current = lsd_netif_get();
        bool ready = lsd_network_is_ready();
        const char *if_name = (current == LSD_IF_WIFI) ? "WiFi" :
                              (current == LSD_IF_4G) ? "4G" : "NONE";
        ESP_LOGI(TAG, "LTE net status: if=%s ready=%s", if_name, ready ? "YES" : "NO");
    }
}

static esp_err_t start_lte_net_test(void)
{
    ESP_LOGI(TAG, "LTE net test: powering on LTE module");
    esp_err_t ret = lte_hal_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "LTE net test: lte_hal_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = lte_hal_power_on();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "LTE net test: power on failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == ret || ESP_ERR_NVS_NEW_VERSION_FOUND == ret) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "LTE net test: NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_netif_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "LTE net test: netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "LTE net test: event loop create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "LTE net test: wifi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LTE net test: initializing net_mgmt (4G enabled)");
    ret = lsd_network_mgmt_init(true);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "LTE net test: lsd_network_mgmt_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    lsd_net_register_switch_cb(lte_net_switch_cb);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &lte_wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &lte_wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_LAIWFS300_LTE_NET_TEST_WIFI_SSID,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_LAIWFS300_LTE_NET_TEST_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "LTE net test: WiFi started (SSID=%s), 4G enabled, monitoring...",
             CONFIG_LAIWFS300_LTE_NET_TEST_WIFI_SSID);

    xTaskCreate(lte_net_status_task, "lte_status", 3072, NULL, tskIDLE_PRIORITY + 2, NULL);

    return ESP_OK;
}
#endif

esp_err_t bringup_test_start(void)
{
    ESP_LOGI(TAG, "bring-up test component starting");
    ESP_LOGI(TAG, "serial log expected on UART0 115200 8N1 through CH340E");
    ESP_LOGI(TAG, "bring-up mode: motor_smoke=%s display_color=%s",
#if CONFIG_LAIWFS300_ENABLE_MOTOR_SMOKE_TEST
             "enabled",
#else
             "disabled",
#endif
#if CONFIG_LAIWFS300_ENABLE_DISPLAY_COLOR_TEST
             "enabled"
#else
             "disabled"
#endif
    );

    /* Motor FIRST: rule out interference from other peripherals */
    esp_err_t motor_ret = run_motor_smoke_test_if_enabled();
    if (ESP_OK != motor_ret) {
        ESP_LOGW(TAG, "motor test failed: %s", esp_err_to_name(motor_ret));
    }

#if CONFIG_LAIWFS300_BRINGUP_TEST_LOG_TASK
    BaseType_t created = xTaskCreate(bringup_log_task, "bringup_log", 3072, NULL, tskIDLE_PRIORITY + 1, NULL);
    if (pdPASS != created) {
        ESP_LOGW(TAG, "continuous bring-up log task creation failed, continue without it");
    }
#else
    ESP_LOGI(TAG, "continuous bring-up logs disabled by Kconfig");
#endif

    esp_err_t display_ret = start_display_color_test_if_enabled();
    if (display_ret != ESP_OK) {
        ESP_LOGW(TAG, "display bring-up skipped or failed: %s", esp_err_to_name(display_ret));
    }

    esp_err_t camera_ret = start_camera_preview_if_enabled();
    if (camera_ret != ESP_OK) {
        ESP_LOGW(TAG, "camera preview skipped or failed: %s", esp_err_to_name(camera_ret));
    }

    esp_err_t touch_ret = start_touch_test_if_enabled();
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "touch test skipped or failed: %s", esp_err_to_name(touch_ret));
    }

    run_rtc_test();
    run_imu_test();
#if CONFIG_LAIWFS300_ENABLE_IMU_TEST
    xTaskCreate(imu_continuous_task, "imu_read", 3072, NULL, tskIDLE_PRIORITY + 2, NULL);
#endif
    run_ioex_input_test();

#if CONFIG_LAIWFS300_ENABLE_AUDIO_NS_TEST || CONFIG_LAIWFS300_ENABLE_AUDIO_LOOPBACK_TEST || CONFIG_LAIWFS300_ENABLE_AUDIO_TONE_TEST || CONFIG_LAIWFS300_ENABLE_MIC_TEST
    xTaskCreate((TaskFunction_t)run_audio_test, "audio_test", 8192, NULL, tskIDLE_PRIORITY + 3, NULL);
#endif

#if CONFIG_LAIWFS300_ENABLE_LTE_NET_TEST
    esp_err_t lte_ret = start_lte_net_test();
    if (lte_ret != ESP_OK) {
        ESP_LOGW(TAG, "LTE net test failed: %s", esp_err_to_name(lte_ret));
    }
#endif

    return ESP_OK;
}
