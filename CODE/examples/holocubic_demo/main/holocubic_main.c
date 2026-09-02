#include "holocubic_frames.h"
#include "holocubic_display_domain.h"
#include "holocubic_input.h"
#include "holocubic_model.h"
#include "holocubic_network.h"
#include "holocubic_periodic.h"
#include "holocubic_renderer.h"
#include "holocubic_spectrum.h"
#include "holocubic_startup_policy.h"
#include "holocubic_time.h"
#include "holocubic_weather.h"

#include "aip8563_rtc.h"
#include "bmi260_imu.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "network_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "holocubic_main";
#define HOLO_LCD_PIXEL_CLOCK_HZ 40000000U
#define HOLO_LCD_BUFFER_LINES 80U
#define HOLO_INPUT_PERIOD_MS 50U
#define HOLO_RENDER_PERIOD_MS 50U
#define HOLO_RENDER_STATS_PERIOD_MS 5000U
#define HOLO_TOUCH_MAX_POINTS 2U
#define HOLO_INPUT_DIAG_PERIOD_MS 1000U
#define HOLO_FRAMES_TASK_STACK 4096U
#define HOLO_NETWORK_TASK_STACK 8192U
#define HOLO_INPUT_TASK_STACK 4096U
#define HOLO_TIME_TASK_STACK 3072U
#define HOLO_FRAMES_TASK_PRIORITY 3U
#define HOLO_NETWORK_TASK_PRIORITY 3U
#define HOLO_INPUT_TASK_PRIORITY 4U
#define HOLO_TIME_TASK_PRIORITY 2U

typedef struct {
    holocubic_model_t model;
    holocubic_weather_t weather;
    SemaphoreHandle_t model_mutex;
    SemaphoreHandle_t weather_mutex;
    holocubic_frames_t frames;
    holocubic_renderer_t renderer;
    holocubic_network_t network;
    bool time_valid;
    char clock_text[16];
    char date_text[16];
} holocubic_app_t;

static holocubic_app_t s_app;
static bool s_touch_ready;

static esp_err_t create_task_with_stack_memory(
    TaskFunction_t entry,
    const char *name,
    uint32_t stack_size,
    void *argument,
    UBaseType_t priority,
    holocubic_task_stack_memory_t stack_memory)
{
    if (NULL == entry || NULL == name || 0U == stack_size) {
        return ESP_ERR_INVALID_ARG;
    }
    const BaseType_t created = HOLO_TASK_STACK_EXTERNAL == stack_memory ?
        xTaskCreateWithCaps(entry, name, stack_size, argument, priority, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) :
        xTaskCreate(entry, name, stack_size, argument, priority, NULL);
    if (pdPASS != created) {
        ESP_LOGE(TAG,
                 "task create failed name=%s stack=%s bytes=%lu internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
                 name,
                 HOLO_TASK_STACK_EXTERNAL == stack_memory ? "psram" :
                                                             "internal",
                 (unsigned long)stack_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "task started name=%s stack=%s bytes=%lu",
             name,
             HOLO_TASK_STACK_EXTERNAL == stack_memory ? "psram" : "internal",
             (unsigned long)stack_size);
    return ESP_OK;
}

static esp_err_t create_runtime_task(holocubic_runtime_task_t task,
                                     TaskFunction_t entry,
                                     const char *name,
                                     uint32_t stack_size,
                                     void *argument,
                                     UBaseType_t priority)
{
    return create_task_with_stack_memory(
        entry, name, stack_size, argument, priority,
        holocubic_runtime_task_stack(task));
}

static uint32_t now_ms(void)
{
    return (uint32_t)((uint64_t)esp_timer_get_time() / 1000ULL);
}

static void dispatch_command(holocubic_command_t command)
{
    if (pdTRUE == xSemaphoreTake(s_app.model_mutex, pdMS_TO_TICKS(20))) {
        (void)holocubic_model_dispatch(&s_app.model, command, now_ms());
        xSemaphoreGive(s_app.model_mutex);
    }
}

static void init_clock_from_rtc(void)
{
    aip8563_time_t rtc = {0};
    struct tm local_time = {0};
    struct timeval system_time = {0};

    if (ESP_OK != aip8563_init() || aip8563_power_lost() ||
        ESP_OK != aip8563_get_time(&rtc)) return;
    holocubic_time_t checked = {
        .year = (uint16_t)(2000U + rtc.year), .month = rtc.month,
        .day = rtc.day, .hours = rtc.hours, .minutes = rtc.minutes,
        .seconds = rtc.seconds,
    };
    if (!holocubic_time_is_valid(&checked)) return;
    local_time.tm_year = (int)checked.year - 1900;
    local_time.tm_mon = checked.month - 1;
    local_time.tm_mday = checked.day;
    local_time.tm_hour = checked.hours;
    local_time.tm_min = checked.minutes;
    local_time.tm_sec = checked.seconds;
    setenv("TZ", "CST-8", 1);
    tzset();
    system_time.tv_sec = mktime(&local_time);
    if (system_time.tv_sec > 0 && 0 == settimeofday(&system_time, NULL)) {
        s_app.time_valid = true;
        ESP_LOGI(TAG, "RTC time restored");
    }
}

static void update_clock_snapshot(void)
{
    time_t current = time(NULL);
    struct tm local_time = {0};
    holocubic_time_t display_time = {0};

    if (current <= 0 || NULL == localtime_r(&current, &local_time)) {
        return;
    }
    display_time = (holocubic_time_t){
        .year = (uint16_t)(local_time.tm_year + 1900),
        .month = (uint8_t)(local_time.tm_mon + 1),
        .day = (uint8_t)local_time.tm_mday,
        .hours = (uint8_t)local_time.tm_hour,
        .minutes = (uint8_t)local_time.tm_min,
        .seconds = (uint8_t)local_time.tm_sec,
    };
    if (!holocubic_time_format(&display_time,
                                s_app.clock_text,
                                sizeof(s_app.clock_text),
                                s_app.date_text,
                                sizeof(s_app.date_text))) {
        s_app.time_valid = false;
    } else {
        s_app.time_valid = true;
    }
}

static void save_system_time_to_rtc(void)
{
    time_t current = time(NULL);
    struct tm local_time = {0};
    holocubic_time_t checked = {0};

    if (current <= 0 || NULL == localtime_r(&current, &local_time)) {
        return;
    }
    checked = (holocubic_time_t){
        .year = (uint16_t)(local_time.tm_year + 1900),
        .month = (uint8_t)(local_time.tm_mon + 1),
        .day = (uint8_t)local_time.tm_mday,
        .hours = (uint8_t)local_time.tm_hour,
        .minutes = (uint8_t)local_time.tm_min,
        .seconds = (uint8_t)local_time.tm_sec,
    };
    if (!holocubic_time_is_valid(&checked) || ESP_OK != aip8563_init()) {
        return;
    }
    const aip8563_time_t rtc = {
        .seconds = checked.seconds,
        .minutes = checked.minutes,
        .hours = checked.hours,
        .day = checked.day,
        .weekday = (uint8_t)local_time.tm_wday,
        .month = checked.month,
        .year = (uint8_t)(checked.year - 2000U),
    };
    if (ESP_OK == aip8563_set_time(&rtc)) {
        ESP_LOGI(TAG, "SNTP time saved to RTC");
    } else {
        ESP_LOGW(TAG, "SNTP time could not be saved to RTC");
    }
}

static void time_task(void *argument)
{
    bool sntp_completion_latched = false;

    (void)argument;
    for (;;) {
        const bool sync_completed =
            SNTP_SYNC_STATUS_COMPLETED == esp_sntp_get_sync_status();

        update_clock_snapshot();
        if (holocubic_time_sync_should_apply(sync_completed,
                                             &sntp_completion_latched)) {
            update_clock_snapshot();
            save_system_time_to_rtc();
        }
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static void input_task(void *argument)
{
    holocubic_touch_gesture_t touch = {0};
    holocubic_imu_gesture_t imu = {0};
    esp_err_t imu_result = ESP_OK;
    uint32_t last_imu_diag_ms = 0U;
    bool calibration_reported = false;
    bool imu_ready = false;
    (void)argument;
    imu_result = bmi260_init();
    imu_ready = ESP_OK == imu_result;
    if (imu_ready) {
        ESP_LOGI(TAG, "BMI260 input initialized");
    } else {
        ESP_LOGW(TAG, "BMI260 input unavailable: %s",
                 esp_err_to_name(imu_result));
    }
    holocubic_imu_init(&imu);
    for (;;) {
        if (s_touch_ready) {
            touch_panel_point_t point = {0};
            uint8_t count = 0U;
            if (ESP_OK == touch_panel_read_point(&point, &count) && count > 0U &&
                count <= HOLO_TOUCH_MAX_POINTS) {
                const int16_t x = (int16_t)(BOARD_LAIWFS300_LCD_V_RES - 1U - point.y);
                const int16_t y = (int16_t)point.x;
                if (!touch.active) holocubic_touch_begin(&touch, x, y);
                holocubic_touch_event_t event = holocubic_touch_update(&touch, x, y);
                if (HOLO_TOUCH_NEXT == event) dispatch_command(HOLO_COMMAND_NEXT);
                else if (HOLO_TOUCH_PREVIOUS == event) dispatch_command(HOLO_COMMAND_PREVIOUS);
            } else if (touch.active && HOLO_TOUCH_CONFIRM == holocubic_touch_end(&touch)) {
                dispatch_command(HOLO_COMMAND_CONFIRM);
            }
        }
        if (imu_ready) {
            bmi260_raw_data_t accel = {0};
            imu_result = bmi260_read_accel(&accel);
            const uint32_t input_now_ms = now_ms();
            if (ESP_OK == imu_result) {
                holocubic_imu_event_t event = holocubic_imu_update(
                    &imu, accel.x, accel.y, input_now_ms);
                if (imu.calibrated && !calibration_reported) {
                    ESP_LOGI(TAG, "BMI260 baseline accel_x=%d accel_y=%d",
                             imu.baseline_x, imu.baseline_y);
                    calibration_reported = true;
                }
                if (imu.calibrated &&
                    (uint32_t)(input_now_ms - last_imu_diag_ms) >=
                        HOLO_INPUT_DIAG_PERIOD_MS) {
                    ESP_LOGI(TAG,
                             "imu sample accel_x=%d accel_y=%d rel_x=%ld rel_y=%ld",
                             accel.x, accel.y,
                             (long)((int32_t)accel.x - imu.baseline_x),
                             (long)((int32_t)accel.y - imu.baseline_y));
                    last_imu_diag_ms = input_now_ms;
                }
                if (HOLO_IMU_NEXT == event) {
                    ESP_LOGI(TAG, "imu event=NEXT accel_x=%d accel_y=%d",
                             accel.x, accel.y);
                    dispatch_command(HOLO_COMMAND_NEXT);
                } else if (HOLO_IMU_PREVIOUS == event) {
                    ESP_LOGI(TAG, "imu event=PREVIOUS accel_x=%d accel_y=%d",
                             accel.x, accel.y);
                    dispatch_command(HOLO_COMMAND_PREVIOUS);
                } else if (HOLO_IMU_CONFIRM == event) {
                    ESP_LOGI(TAG, "imu event=CONFIRM accel_x=%d accel_y=%d",
                             accel.x, accel.y);
                    dispatch_command(HOLO_COMMAND_CONFIRM);
                }
            } else if ((uint32_t)(input_now_ms - last_imu_diag_ms) >=
                       HOLO_INPUT_DIAG_PERIOD_MS) {
                ESP_LOGW(TAG, "BMI260 read failed: %s",
                         esp_err_to_name(imu_result));
                last_imu_diag_ms = input_now_ms;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(HOLO_INPUT_PERIOD_MS));
    }
}

static void render_task(void *argument)
{
    holocubic_periodic_t periodic = {0};
    holocubic_present_gate_t present_gate = {0};
    uint64_t report_started_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    uint32_t presented_frames = 0U;
    uint32_t present_failures = 0U;
    uint32_t skipped_frames = 0U;
    uint32_t total_present_ms = 0U;
    uint32_t max_present_ms = 0U;

    (void)argument;
    (void)holocubic_periodic_init(&periodic, report_started_ms,
                                  HOLO_RENDER_PERIOD_MS);
    for (;;) {
        holocubic_page_t page = HOLO_PAGE_ANIMATION;
        uint8_t spectrum_mode = 0U;
        uint32_t page_revision = 0U;
        {
            holocubic_weather_t weather = {0};
            holocubic_frame_snapshot_t frame_snapshot = {0};
            holocubic_spectrum_snapshot_t spectrum_snapshot = {0};
            bool has_spectrum = false;
            if (pdTRUE == xSemaphoreTake(s_app.model_mutex, pdMS_TO_TICKS(20))) {
                page = s_app.model.page;
                spectrum_mode = s_app.model.spectrum_mode;
                page_revision = s_app.model.revision;
                xSemaphoreGive(s_app.model_mutex);
            }
            if (pdTRUE == xSemaphoreTake(s_app.weather_mutex, pdMS_TO_TICKS(20))) {
                weather = s_app.weather;
                xSemaphoreGive(s_app.weather_mutex);
            }
            const bool has_frame = HOLO_PAGE_ANIMATION == page &&
                holocubic_frames_snapshot(&s_app.frames, &frame_snapshot);
            if (HOLO_PAGE_SPECTRUM == page) {
                has_spectrum = holocubic_spectrum_snapshot(&spectrum_snapshot);
            }
            const uint32_t render_now_ms = now_ms();
            const uint32_t content_revision = holocubic_content_revision(
                page, has_frame, frame_snapshot.revision, weather.revision,
                has_spectrum, spectrum_snapshot.revision, spectrum_mode,
                render_now_ms, s_app.time_valid);
            if (holocubic_present_gate_should_present(
                    &present_gate, page_revision, content_revision)) {
                holocubic_renderer_draw(&s_app.renderer, page, &weather,
                                        s_app.clock_text, s_app.date_text,
                                        s_app.time_valid,
                                        has_frame ? frame_snapshot.pixels : NULL,
                                        has_spectrum ? &spectrum_snapshot : NULL,
                                        (holocubic_spectrum_mode_t)spectrum_mode,
                                        render_now_ms,
                                        has_frame ? frame_snapshot.revision :
                                                    render_now_ms / 100U);
                const uint32_t present_started_ms = now_ms();
                if (ESP_OK == holocubic_renderer_present(&s_app.renderer)) {
                    const uint32_t present_elapsed_ms = now_ms() - present_started_ms;
                    holocubic_present_gate_mark_presented(
                        &present_gate, page_revision, content_revision);
                    presented_frames++;
                    total_present_ms += present_elapsed_ms;
                    if (max_present_ms < present_elapsed_ms) {
                        max_present_ms = present_elapsed_ms;
                    }
                } else {
                    present_failures++;
                }
            } else {
                skipped_frames++;
            }
        }

        const uint64_t current_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
        const uint64_t elapsed_ms = current_ms - report_started_ms;
        if (HOLO_RENDER_STATS_PERIOD_MS <= elapsed_ms &&
            (0U < presented_frames || 0U < present_failures)) {
            const uint32_t fps_x10 = (uint32_t)(
                ((uint64_t)presented_frames * 10000ULL) / elapsed_ms);
            const uint32_t average_present_ms = 0U < presented_frames ?
                total_present_ms / presented_frames : 0U;
            ESP_LOGI(TAG, "render presented=%lu fps=%lu.%lu avg_ms=%lu max_ms=%lu skipped=%lu failures=%lu",
                     (unsigned long)presented_frames,
                     (unsigned long)(fps_x10 / 10U),
                     (unsigned long)(fps_x10 % 10U),
                     (unsigned long)average_present_ms,
                     (unsigned long)max_present_ms,
                     (unsigned long)skipped_frames,
                     (unsigned long)present_failures);
            presented_frames = 0U;
            present_failures = 0U;
            skipped_frames = 0U;
            total_present_ms = 0U;
            max_present_ms = 0U;
            report_started_ms = current_ms;
        }

        const uint32_t delay_ms = holocubic_periodic_next_delay(
            &periodic, (uint64_t)esp_timer_get_time() / 1000ULL);
        if (0U < delay_ms) vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == result || ESP_ERR_NVS_NEW_VERSION_FOUND == result) {
        (void)nvs_flash_erase();
        result = nvs_flash_init();
    }
    if (ESP_OK != result) ESP_LOGW(TAG, "NVS degraded: %s", esp_err_to_name(result));
    if (ESP_OK != board_laiwfs300_init()) {
        ESP_LOGE(TAG, "board init failed");
        return;
    }
    result = board_laiwfs300_display_init_with_config(HOLO_LCD_PIXEL_CLOCK_HZ,
                                                      HOLO_LCD_BUFFER_LINES);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(result));
        return;
    }
    result = display_hal_set_orientation(true, false, true);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "display orientation failed: %s", esp_err_to_name(result));
        return;
    }
    (void)display_hal_fill_rgb565(0x0000U);
    result = holocubic_display_domain_init();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "display domain init failed: %s", esp_err_to_name(result));
        return;
    }
    holocubic_model_init(&s_app.model);
    s_app.model_mutex = xSemaphoreCreateMutex();
    s_app.weather_mutex = xSemaphoreCreateMutex();
    if (NULL == s_app.model_mutex || NULL == s_app.weather_mutex) return;
    s_app.weather.state = HOLO_WEATHER_OFFLINE;
    (void)snprintf(s_app.clock_text, sizeof(s_app.clock_text), "--:--:--");
    (void)snprintf(s_app.date_text, sizeof(s_app.date_text), "TIME UNSYNCED");
    init_clock_from_rtc();
    if (ESP_OK != holocubic_renderer_init(&s_app.renderer)) return;
    const esp_err_t frames_prepare_result =
        holocubic_frames_prepare(&s_app.frames);
    if (ESP_OK != frames_prepare_result) {
        ESP_LOGW(TAG, "frame buffers unavailable: %s",
                 esp_err_to_name(frames_prepare_result));
    }
    holocubic_startup_step_t startup_steps[HOLO_STARTUP_STEP_COUNT] = {0};
    if (!holocubic_startup_plan(startup_steps, HOLO_STARTUP_STEP_COUNT)) {
        ESP_LOGE(TAG, "startup plan unavailable");
        return;
    }
    bool runtime_tasks_ready = true;
    for (size_t index = 0U; index < HOLO_STARTUP_STEP_COUNT; ++index) {
        switch (startup_steps[index]) {
        case HOLO_STARTUP_STEP_NETWORK:
            /* USB ECM must reserve internal DMA resources before audio. */
            if (!holocubic_network_init(&s_app.network, &s_app.weather,
                                        s_app.weather_mutex)) {
                ESP_LOGW(TAG, "network initialization unavailable");
                runtime_tasks_ready = false;
                break;
            }
            /* Allocate the internal-RAM network stack before PSRAM preload. */
            if (ESP_OK != create_runtime_task(
                    HOLO_RUNTIME_TASK_NETWORK, holocubic_network_task,
                    "holo_network", HOLO_NETWORK_TASK_STACK, &s_app.network,
                    HOLO_NETWORK_TASK_PRIORITY)) {
                runtime_tasks_ready = false;
            }
            break;
        case HOLO_STARTUP_STEP_TOUCH:
            /* Touch owns shared I2C initialization before audio starts. */
            s_touch_ready = ESP_OK == board_laiwfs300_touch_init();
            if (!s_touch_ready) {
                ESP_LOGW(TAG, "touch unavailable; main touch disabled");
            }
            break;
        case HOLO_STARTUP_STEP_FRAMES:
            if (ESP_OK == frames_prepare_result) {
                (void)holocubic_frames_load(&s_app.frames);
            }
            break;
        case HOLO_STARTUP_STEP_SPECTRUM:
            result = holocubic_spectrum_start();
            if (ESP_OK != result) {
                ESP_LOGW(TAG, "spectrum task unavailable: %s",
                         esp_err_to_name(result));
            }
            break;
        case HOLO_STARTUP_STEP_RENDER:
            result = create_task_with_stack_memory(
                render_task, "holo_render", 8192U, NULL, 2U,
                holocubic_startup_task_stack(HOLO_STARTUP_STEP_RENDER));
            if (ESP_OK != result) {
                ESP_LOGE(TAG, "render task unavailable");
                return;
            }
            ESP_LOGI(TAG, "render task started before Flash preload");
            break;
        default:
            ESP_LOGE(TAG, "unknown startup step=%u",
                     (unsigned)startup_steps[index]);
            return;
        }
    }
    if (ESP_OK == frames_prepare_result &&
        ESP_OK != create_runtime_task(
            HOLO_RUNTIME_TASK_FRAMES, holocubic_frames_task, "holo_frames",
            HOLO_FRAMES_TASK_STACK, &s_app.frames,
            HOLO_FRAMES_TASK_PRIORITY)) {
        runtime_tasks_ready = false;
    }
    if (ESP_OK != create_runtime_task(
            HOLO_RUNTIME_TASK_INPUT, input_task, "holo_input",
            HOLO_INPUT_TASK_STACK, NULL, HOLO_INPUT_TASK_PRIORITY)) {
        runtime_tasks_ready = false;
    }
    if (ESP_OK != create_runtime_task(
            HOLO_RUNTIME_TASK_TIME, time_task, "holo_time",
            HOLO_TIME_TASK_STACK, NULL, HOLO_TIME_TASK_PRIORITY)) {
        runtime_tasks_ready = false;
    }
    if (runtime_tasks_ready) {
        ESP_LOGI(TAG,
                 "HoloCubic ready: home=(40,0,240,240), network=4G-only");
    } else {
        ESP_LOGE(TAG, "HoloCubic degraded: runtime task startup incomplete");
    }
}
