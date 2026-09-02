/**
 * Camera Face Detect Demo: SP0A39 live preview + local face detection overlay.
 */

extern "C" {
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "camera_hal.h"
#include "display_hal.h"
}

#include "dl_image.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_cache.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "flash_param_face_detect.hpp"
#include "servo_tracking_logic.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <list>
#include <new>
#include <vector>

static const char *TAG = "cam_face_demo";

#define CAM_H_RES                    640
#define CAM_V_RES                    480
#define CAM_FRAME_BYTES              (CAM_H_RES * CAM_V_RES * 2U)
#define LCD_H_RES                    BOARD_LAIWFS300_LCD_H_RES
#define LCD_V_RES                    BOARD_LAIWFS300_LCD_V_RES
#define PREVIEW_H_RES                LCD_H_RES
#define PREVIEW_V_RES                LCD_V_RES
#define PREVIEW_X_OFFSET             0
#define PREVIEW_Y_OFFSET             0
#define FB_COUNT                     3
#define LCD_CHUNK_BUF_CAPACITY       1
#define LCD_DRAW_WAIT_TIMEOUT_MS     200
#define LCD_PREVIEW_PIXEL_CLOCK_HZ   20000000
#define LCD_CHUNK_LINES              80
#define SERIAL_MONITOR_WAIT_MS       1000
#define CAMERA_DIAG_INTERVAL_MS      5000
#define CAMERA_DIAG_SAMPLE_COUNT     100000
#define FIXED_COLOR_BARS_DIAG_ENABLED 0
#define FROZEN_CAMERA_FRAME_DIAG_ENABLED 0
#define DETACHED_RGB_FRAME_DIAG_ENABLED 0
#define PAUSE_CAPTURE_DURING_RGB_DETACH_DIAG_ENABLED 0
#define FACE_DETECTION_ENABLED       1
#define FACE_DETECT_INTERVAL_FRAMES  8
#define FACE_DETECT_TASK_STACK_BYTES 16384
#define FACE_DETECT_TASK_PRIORITY    1
#define FACE_DETECT_TASK_CORE        1
#define FACE_MAX_BOXES               8
#define FACE_BOX_THICKNESS           2
#define FACE_TARGET_NONE             (-1)
#define FACE_TARGET_COLOR_R          255
#define FACE_TARGET_COLOR_G          0
#define FACE_TARGET_COLOR_B          0
#define FACE_OTHER_COLOR_R           0
#define FACE_OTHER_COLOR_G           255
#define FACE_OTHER_COLOR_B           0
#define FACE_INPUT_DIAG_GRID_COLS    4
#define FACE_INPUT_DIAG_GRID_ROWS    4
#define FACE_INPUT_DIAG_FIRST_COUNT  2
#define FACE_INPUT_DIAG_INTERVAL     16
#define FACE_INPUT_DIAG_ENABLED      0
#define FACE_RUNTIME_VERBOSE_LOG_ENABLED 0
#define FACE_BOX_OVERLAY_ENABLED     1
#define CAPTURE_GATE_DIAG_FIRST_COUNT 3
#define CAPTURE_GATE_DIAG_INTERVAL   16
#define FACE_DETECT_SCORE_THR        0.20f
#define FACE_DETECT_NMS_THR          0.50f
#define FACE_EDGE_SHIFT_PERCENT      20
#define PREVIEW_STATS_INTERVAL_MS    10000
#define DVP_DMA_BURST_SIZE           64
#define SERVO_INPUT_SNAPSHOT_ENABLED 1
#define SERVO_CONTROL_TASK_ENABLED   1
#define SERVO_INPUT_READ_ENABLED     1
#define SERVO_CONTROL_STEP_ENABLED   0
#define SERVO_CONTROL_LOG_ENABLED    0
#define SERVO_PWM_OUTPUT_ENABLED     0
#define SERVO_TRACKING_OUTPUT_ENABLED 0
#define SERVO_SIGNAL_GPIO            BOARD_LAIWFS300_GPIO_MOTOR_IN3
#define SERVO_SAFE_LOW_GPIO          BOARD_LAIWFS300_GPIO_MOTOR_IN4
#define SERVO_LEDC_MODE              LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER             LEDC_TIMER_1
#define SERVO_LEDC_CHANNEL           LEDC_CHANNEL_4
#define SERVO_PWM_FREQ_HZ            50
#define SERVO_PWM_PERIOD_US          (1000000 / SERVO_PWM_FREQ_HZ)
#define SERVO_PWM_DUTY_RES_BITS      14
#define SERVO_PWM_DUTY_RESOLUTION    LEDC_TIMER_14_BIT
#define SERVO_PWM_DUTY_MAX           ((1U << SERVO_PWM_DUTY_RES_BITS) - 1U)
#define SERVO_MIN_PULSE_US           1000
#define SERVO_MAX_PULSE_US           2000
#define SERVO_MIN_ANGLE_DEG          45
#define SERVO_CENTER_ANGLE_DEG       90
#define SERVO_MAX_ANGLE_DEG          135
#define SERVO_SCREEN_CENTER_Y        ((int)PREVIEW_V_RES / 2)
#define SERVO_INVALID_CENTER_Y       (-1)
#define SERVO_FACE_DEADZONE_PX       18
#define SERVO_TRACK_GAIN_NUM         1
#define SERVO_TRACK_GAIN_DEN         4
#define SERVO_DIRECTION_SIGN         1
#define SERVO_TARGET_STEP_MAX_DEG    8
#define SERVO_APPLY_STEP_MAX_DEG     3
#define SERVO_UPDATE_INTERVAL_MS     120
#define SERVO_INPUT_STALE_MS         1500
#define SERVO_LOST_HOLD_MS           4000
#define SERVO_TASK_STACK_BYTES       4096
#define SERVO_TASK_PRIORITY          1
#define SERVO_BOOT_SELF_TEST_ENABLED 0
#define SERVO_BOOT_TEST_DELTA_DEG    5
#define SERVO_BOOT_TEST_SETTLE_MS    30
#define SERVO_BOOT_TEST_DELAY_MS     700

#if SERVO_TRACKING_OUTPUT_ENABLED && !SERVO_PWM_OUTPUT_ENABLED
#error "SERVO_TRACKING_OUTPUT_ENABLED requires SERVO_PWM_OUTPUT_ENABLED"
#endif

#if SERVO_CONTROL_TASK_ENABLED && !SERVO_INPUT_SNAPSHOT_ENABLED
#error "SERVO_CONTROL_TASK_ENABLED requires SERVO_INPUT_SNAPSHOT_ENABLED"
#endif

#if SERVO_CONTROL_LOG_ENABLED && !SERVO_CONTROL_TASK_ENABLED
#error "SERVO_CONTROL_LOG_ENABLED requires SERVO_CONTROL_TASK_ENABLED"
#endif

#if SERVO_INPUT_READ_ENABLED && !SERVO_CONTROL_TASK_ENABLED
#error "SERVO_INPUT_READ_ENABLED requires SERVO_CONTROL_TASK_ENABLED"
#endif

#if SERVO_CONTROL_STEP_ENABLED && !SERVO_INPUT_READ_ENABLED
#error "SERVO_CONTROL_STEP_ENABLED requires SERVO_INPUT_READ_ENABLED"
#endif

#if (SERVO_CONTROL_LOG_ENABLED || SERVO_TRACKING_OUTPUT_ENABLED) && !SERVO_CONTROL_STEP_ENABLED
#error "Servo logging and tracking output require SERVO_CONTROL_STEP_ENABLED"
#endif

#if SERVO_BOOT_SELF_TEST_ENABLED && !SERVO_PWM_OUTPUT_ENABLED
#error "SERVO_BOOT_SELF_TEST_ENABLED requires SERVO_PWM_OUTPUT_ENABLED"
#endif

#if PAUSE_CAPTURE_DURING_RGB_DETACH_DIAG_ENABLED && !DETACHED_RGB_FRAME_DIAG_ENABLED
#error "PAUSE_CAPTURE_DURING_RGB_DETACH_DIAG_ENABLED requires DETACHED_RGB_FRAME_DIAG_ENABLED"
#endif

#if DETACHED_RGB_FRAME_DIAG_ENABLED && FACE_DETECTION_ENABLED
#error "Detached RGB diagnostics require face detection to remain disabled"
#endif

typedef struct {
    int x1;
    int y1;
    int x2;
    int y2;
    float score;
    bool tracked;
} face_box_t;

typedef enum {
    PIXFMT_VYUY = 0,
} yuv422_pixfmt_t;

typedef enum {
    FACE_SELECT_NONE = 0,
    FACE_SELECT_SINGLE,
    FACE_SELECT_RANDOM,
    FACE_SELECT_NEAREST,
} face_select_reason_t;

typedef enum {
    FACE_INPUT_PREVIEW_BGR_FOR_RGB_SWAP = 0,
    FACE_INPUT_PREVIEW_RGB_RAW,
    FACE_INPUT_LANDSCAPE_BGR_FOR_RGB_SWAP,
    FACE_INPUT_LANDSCAPE_RGB_RAW,
    FACE_INPUT_VARIANT_COUNT,
} face_input_variant_t;

typedef enum {
    FACE_SCAN_FULL = 0,
    FACE_SCAN_LEFT_EDGE,
    FACE_SCAN_RIGHT_EDGE,
    FACE_SCAN_WINDOW_COUNT,
} face_scan_window_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int shift_x;
} face_scan_roi_t;

typedef struct {
    uint32_t display_frame;
    uint32_t source_sequence;
    uint64_t ready_us;
    uint64_t capture_pause_started_us;
    uint32_t capture_pause_cycle;
    const uint8_t *framebuffer;
    face_input_variant_t variant;
    face_scan_window_t scan;
    face_scan_roi_t roi;
} face_detect_request_t;

typedef struct {
    bool active;
    int buffer_idx;
    uint64_t started_us;
} lcd_pending_draw_t;

typedef struct {
    face_box_t faces[FACE_MAX_BOXES];
    int face_count;
    int tracked_face_index;
    int tracked_center_x;
    int tracked_center_y;
    bool tracked_face_valid;
    uint32_t source_sequence;
    uint32_t display_frame;
} face_result_snapshot_t;

static esp_cam_ctlr_handle_t s_cam_ctlr;
static uint8_t *s_fb[FB_COUNT];
static uint8_t *s_frozen_camera_frame;
static bool s_frozen_camera_frame_ready;
static uint32_t s_frozen_camera_frame_sequence;
static uint16_t *s_detached_rgb_frame;
static uint16_t *s_rgb_chunk_buf[LCD_CHUNK_BUF_CAPACITY];
static int s_rgb_chunk_buf_count;
static uint16_t *s_face_rgb565;
static SemaphoreHandle_t s_frame_ready;
static QueueHandle_t s_face_detect_queue;
static SemaphoreHandle_t s_face_rgb_available;
static TaskHandle_t s_face_detect_task_handle;
#if SERVO_CONTROL_TASK_ENABLED
static TaskHandle_t s_servo_task_handle;
#endif
static portMUX_TYPE s_framebuffer_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_face_result_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_detect_metrics_lock = portMUX_INITIALIZER_UNLOCKED;
#if SERVO_INPUT_SNAPSHOT_ENABLED
static portMUX_TYPE s_servo_input_lock = portMUX_INITIALIZER_UNLOCKED;
#endif
static volatile int s_active_fb;
static volatile int s_locked_fb = -1;
static volatile int s_latest_completed_fb = -1;
static volatile uint32_t s_fb_completed_sequence[FB_COUNT];
static volatile uint32_t s_frame_count;
static volatile uint32_t s_error_count;
static uint32_t s_last_display_sequence;
static uint32_t s_display_frame_count;
static uint32_t s_detect_frame_count;
static uint32_t s_detect_error_count;
static uint32_t s_detect_request_count;
static uint32_t s_detect_request_drop_count;
static uint32_t s_capture_pause_count;
static uint32_t s_capture_pause_error_count;
static uint64_t s_capture_pause_us;
static uint64_t s_detect_prepare_us;
static volatile bool s_capture_paused;
static const yuv422_pixfmt_t s_pixfmt = PIXFMT_VYUY;
static dl::detect::Detect *s_face_model;

static face_box_t s_faces[FACE_MAX_BOXES];
static int s_face_count;
static int s_tracked_face_index = FACE_TARGET_NONE;
static int s_tracked_center_x;
static int s_tracked_center_y;
static bool s_tracked_face_valid;
static uint64_t s_last_detect_us;
static uint64_t s_window_detect_us;
static face_input_variant_t s_face_input_variant = FACE_INPUT_LANDSCAPE_BGR_FOR_RGB_SWAP;
static bool s_face_input_variant_locked = false;
static uint32_t s_face_input_attempts[FACE_INPUT_VARIANT_COUNT];
static uint32_t s_face_input_hits[FACE_INPUT_VARIANT_COUNT];
static face_scan_window_t s_next_edge_scan = FACE_SCAN_LEFT_EDGE;
static face_scan_window_t s_next_face_scan = FACE_SCAN_FULL;
static uint32_t s_face_scan_attempts[FACE_SCAN_WINDOW_COUNT];
static uint32_t s_face_scan_hits[FACE_SCAN_WINDOW_COUNT];
#if SERVO_INPUT_SNAPSHOT_ENABLED
static servo_face_input_t s_latest_servo_face_input;
#endif
static face_result_snapshot_t s_published_face_result;
static face_box_t s_display_faces[FACE_MAX_BOXES];
static int s_display_face_count;
static uint32_t s_display_face_source_sequence;
static uint32_t s_display_face_result_frame;

#if SERVO_CONTROL_TASK_ENABLED
static const servo_control_config_t s_servo_control_config = {
    .min_center_y = 0,
    .max_center_y = (int32_t)PREVIEW_V_RES - 1,
    .screen_center_y = SERVO_SCREEN_CENTER_Y,
    .min_angle_deg = SERVO_MIN_ANGLE_DEG,
    .center_angle_deg = SERVO_CENTER_ANGLE_DEG,
    .max_angle_deg = SERVO_MAX_ANGLE_DEG,
    .deadzone_px = SERVO_FACE_DEADZONE_PX,
    .gain_num = SERVO_TRACK_GAIN_NUM,
    .gain_den = SERVO_TRACK_GAIN_DEN,
    .direction_sign = SERVO_DIRECTION_SIGN,
    .target_step_max_deg = SERVO_TARGET_STEP_MAX_DEG,
    .apply_step_max_deg = SERVO_APPLY_STEP_MAX_DEG,
    .stale_after_ms = SERVO_INPUT_STALE_MS,
    .return_center_after_ms = SERVO_LOST_HOLD_MS,
};
#endif

static int32_t s_y_term_lut[256];
static int32_t s_u_to_b_lut[256];
static int32_t s_u_to_g_lut[256];
static int32_t s_v_to_r_lut[256];
static int32_t s_v_to_g_lut[256];

static const char *pixfmt_name(yuv422_pixfmt_t pixfmt)
{
    switch (pixfmt) {
    case PIXFMT_VYUY:
        return "VYUY";
    default:
        return "UNKNOWN";
    }
}

static const char *face_select_reason_name(face_select_reason_t reason)
{
    switch (reason) {
    case FACE_SELECT_NONE:
        return "none";
    case FACE_SELECT_SINGLE:
        return "single";
    case FACE_SELECT_RANDOM:
        return "random";
    case FACE_SELECT_NEAREST:
        return "nearest";
    default:
        return "unknown";
    }
}

static const char *face_input_variant_name(face_input_variant_t variant)
{
    switch (variant) {
    case FACE_INPUT_PREVIEW_BGR_FOR_RGB_SWAP:
        return "preview_bgr_for_rgb_swap";
    case FACE_INPUT_PREVIEW_RGB_RAW:
        return "preview_rgb_raw";
    case FACE_INPUT_LANDSCAPE_BGR_FOR_RGB_SWAP:
        return "landscape_bgr_for_rgb_swap";
    case FACE_INPUT_LANDSCAPE_RGB_RAW:
        return "landscape_rgb_raw";
    default:
        return "unknown";
    }
}

static const char *face_scan_window_name(face_scan_window_t scan)
{
    switch (scan) {
    case FACE_SCAN_FULL:
        return "full";
    case FACE_SCAN_LEFT_EDGE:
        return "left_edge";
    case FACE_SCAN_RIGHT_EDGE:
        return "right_edge";
    default:
        return "unknown";
    }
}

static int face_input_variant_index(face_input_variant_t variant)
{
    return (int)variant;
}

static int face_scan_window_index(face_scan_window_t scan)
{
    return (int)scan;
}

static bool face_input_variant_is_landscape(face_input_variant_t variant)
{
    return (FACE_INPUT_LANDSCAPE_BGR_FOR_RGB_SWAP == variant) || (FACE_INPUT_LANDSCAPE_RGB_RAW == variant);
}

static bool face_input_variant_uses_bgr(face_input_variant_t variant)
{
    return (FACE_INPUT_PREVIEW_BGR_FOR_RGB_SWAP == variant) || (FACE_INPUT_LANDSCAPE_BGR_FOR_RGB_SWAP == variant);
}

static uint16_t get_face_input_width(face_input_variant_t variant)
{
    return face_input_variant_is_landscape(variant) ? PREVIEW_V_RES : PREVIEW_H_RES;
}

static uint16_t get_face_input_height(face_input_variant_t variant)
{
    return face_input_variant_is_landscape(variant) ? PREVIEW_H_RES : PREVIEW_V_RES;
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint8_t clamp_u8(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (uint8_t)value;
}

static face_scan_roi_t make_face_scan_roi(face_scan_window_t scan)
{
    const int edge_shift = clamp_int(((int)PREVIEW_H_RES * FACE_EDGE_SHIFT_PERCENT) / 100,
                                     1,
                                     ((int)PREVIEW_H_RES / 2) - 1);
    face_scan_roi_t roi = {
        .x = 0,
        .y = 0,
        .w = (int)PREVIEW_H_RES,
        .h = (int)PREVIEW_V_RES,
        .shift_x = 0,
    };

    switch (scan) {
    case FACE_SCAN_LEFT_EDGE:
        roi.shift_x = edge_shift;
        break;
    case FACE_SCAN_RIGHT_EDGE:
        roi.shift_x = -edge_shift;
        break;
    case FACE_SCAN_FULL:
    default:
        break;
    }

    return roi;
}

static int map_axis_from_full_to_roi(int value, int full_extent, int roi_start, int roi_extent)
{
    if (full_extent <= 1 || roi_extent <= 1) {
        return roi_start;
    }

    int mapped = roi_start + (value * (roi_extent - 1)) / (full_extent - 1);
    return clamp_int(mapped, roi_start, roi_start + roi_extent - 1);
}

static face_scan_window_t choose_edge_scan(bool had_previous_face, int previous_center_x)
{
    face_scan_window_t scan = s_next_edge_scan;

    if (had_previous_face) {
        scan = (previous_center_x < ((int)PREVIEW_H_RES / 2)) ? FACE_SCAN_LEFT_EDGE : FACE_SCAN_RIGHT_EDGE;
    }

    s_next_edge_scan = (FACE_SCAN_LEFT_EDGE == scan) ? FACE_SCAN_RIGHT_EDGE : FACE_SCAN_LEFT_EDGE;
    return scan;
}

static gpio_num_t to_gpio_num(int pin)
{
    return (gpio_num_t)pin;
}

#if SERVO_INPUT_SNAPSHOT_ENABLED
static void publish_servo_face_input(const bool valid, const int center_y)
{
    servo_face_input_t input = {};
    input.completed_at_us = (uint64_t)esp_timer_get_time();
    input.valid = valid && (0 <= center_y) && (center_y < (int)PREVIEW_V_RES);
    input.center_y = input.valid ? (int16_t)center_y : (int16_t)SERVO_INVALID_CENTER_Y;

    portENTER_CRITICAL(&s_servo_input_lock);
    input.sequence = s_latest_servo_face_input.sequence + 1U;
    if (0U == input.sequence) {
        input.sequence = 1U;
    }
    s_latest_servo_face_input = input;
    portEXIT_CRITICAL(&s_servo_input_lock);
}
#endif

#if SERVO_CONTROL_TASK_ENABLED
#if SERVO_PWM_OUTPUT_ENABLED || SERVO_CONTROL_LOG_ENABLED
static uint32_t servo_angle_to_pulse_us(int angle)
{
    return map_servo_angle_to_pulse_us(angle,
                                       SERVO_MIN_ANGLE_DEG,
                                       SERVO_MAX_ANGLE_DEG,
                                       SERVO_MIN_PULSE_US,
                                       SERVO_MAX_PULSE_US);
}
#endif

#if SERVO_PWM_OUTPUT_ENABLED
static uint32_t servo_angle_to_duty(int angle)
{
    uint32_t pulse_us = servo_angle_to_pulse_us(angle);
    return ((pulse_us * SERVO_PWM_DUTY_MAX) + (SERVO_PWM_PERIOD_US / 2U)) / SERVO_PWM_PERIOD_US;
}

static esp_err_t servo_apply_angle(int angle)
{
    uint32_t duty = servo_angle_to_duty(angle);
    esp_err_t ret = ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty);
    if (ESP_OK != ret) {
        return ret;
    }
    return ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
}

#if SERVO_BOOT_SELF_TEST_ENABLED
static void log_servo_pwm_output(const char *phase, int angle)
{
    if (NULL == phase) {
        phase = "unknown";
    }

    uint32_t expected_duty = servo_angle_to_duty(angle);
    uint32_t actual_duty = ledc_get_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
    uint32_t freq_hz = ledc_get_freq(SERVO_LEDC_MODE, SERVO_LEDC_TIMER);
    int safe_level = gpio_get_level(to_gpio_num(SERVO_SAFE_LOW_GPIO));
    int signal_level = gpio_get_level(to_gpio_num(SERVO_SIGNAL_GPIO));

    ESP_LOGI(TAG,
             "[servo_test] phase=%s angle=%d pulse_us=%lu duty=%lu/%lu ledc_duty=%lu freq=%luHz signal_gpio=%d signal_level=%d safe_low_gpio=%d safe_level=%d",
             phase,
             angle,
             (unsigned long)servo_angle_to_pulse_us(angle),
             (unsigned long)expected_duty,
             (unsigned long)SERVO_PWM_DUTY_MAX,
             (unsigned long)actual_duty,
             (unsigned long)freq_hz,
             SERVO_SIGNAL_GPIO,
             signal_level,
             SERVO_SAFE_LOW_GPIO,
             safe_level);
}

static esp_err_t run_servo_boot_self_test(void)
{
    const int test_angles[] = {
        SERVO_CENTER_ANGLE_DEG - SERVO_BOOT_TEST_DELTA_DEG,
        SERVO_CENTER_ANGLE_DEG + SERVO_BOOT_TEST_DELTA_DEG,
        SERVO_CENTER_ANGLE_DEG,
    };

    ESP_LOGI(TAG,
             "[servo_test] begin signal_gpio=%d angles=(%d,%d,%d) delay_ms=%d",
             SERVO_SIGNAL_GPIO,
             SERVO_CENTER_ANGLE_DEG - SERVO_BOOT_TEST_DELTA_DEG,
             SERVO_CENTER_ANGLE_DEG + SERVO_BOOT_TEST_DELTA_DEG,
             SERVO_CENTER_ANGLE_DEG,
             SERVO_BOOT_TEST_DELAY_MS);
    for (size_t i = 0; i < (sizeof(test_angles) / sizeof(test_angles[0])); ++i) {
        int angle = test_angles[i];
        ESP_RETURN_ON_ERROR(servo_apply_angle(angle), TAG, "servo boot self-test apply");
        vTaskDelay(pdMS_TO_TICKS(SERVO_BOOT_TEST_SETTLE_MS));
        log_servo_pwm_output("boot_step", angle);
        vTaskDelay(pdMS_TO_TICKS(SERVO_BOOT_TEST_DELAY_MS));
    }
    ESP_LOGI(TAG, "[servo_test] done");
    return ESP_OK;
}
#endif
#endif

static esp_err_t init_servo_pwm_output(void)
{
#if SERVO_PWM_OUTPUT_ENABLED
    if (SERVO_SIGNAL_GPIO == SERVO_SAFE_LOW_GPIO) {
        ESP_LOGE(TAG, "[servo] signal gpio and safe-low gpio conflict: GPIO%d", SERVO_SIGNAL_GPIO);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t safe_gpio = {
        .pin_bit_mask = (1ULL << SERVO_SAFE_LOW_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_reset_pin(to_gpio_num(SERVO_SAFE_LOW_GPIO)), TAG, "servo safe-low gpio reset");
    ESP_RETURN_ON_ERROR(gpio_config(&safe_gpio), TAG, "servo safe-low gpio config");
    ESP_RETURN_ON_ERROR(gpio_set_level(to_gpio_num(SERVO_SAFE_LOW_GPIO), 0), TAG, "servo safe-low gpio set");
    ESP_RETURN_ON_ERROR(gpio_reset_pin(to_gpio_num(SERVO_SIGNAL_GPIO)), TAG, "servo signal gpio reset");
    ESP_RETURN_ON_ERROR(gpio_set_drive_capability(to_gpio_num(SERVO_SIGNAL_GPIO), GPIO_DRIVE_CAP_3),
                        TAG,
                        "servo signal gpio drive capability");

    ledc_timer_config_t timer = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_PWM_DUTY_RESOLUTION,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "servo ledc timer config");

    ledc_channel_config_t channel = {
        .gpio_num = SERVO_SIGNAL_GPIO,
        .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = servo_angle_to_duty(SERVO_CENTER_ANGLE_DEG),
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = 0,
        },
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "servo ledc channel config");
#if SERVO_BOOT_SELF_TEST_ENABLED
    ESP_RETURN_ON_ERROR(run_servo_boot_self_test(), TAG, "servo boot self-test");
#endif
#else
    ESP_LOGI(TAG, "[servo] PWM output disabled; GPIO%d/GPIO%d untouched",
             SERVO_SIGNAL_GPIO, SERVO_SAFE_LOW_GPIO);
#endif
    return ESP_OK;
}

#if SERVO_CONTROL_LOG_ENABLED
static const char *get_servo_output_mode_name(void)
{
#if SERVO_TRACKING_OUTPUT_ENABLED
    return "pwm";
#elif SERVO_PWM_OUTPUT_ENABLED
    return "calibration_hold";
#else
    return "dry_run";
#endif
}

static const char *get_servo_direction_name(const servo_control_state_t *const state)
{
    if (NULL == state) {
        return "hold";
    }
    if (SERVO_CONTROL_RETURN_CENTER == state->mode) {
        return "center";
    }
    if (!state->latest_input_usable) {
        return "hold";
    }

    const int screen_error = SERVO_SCREEN_CENTER_Y - state->latest_center_y;
    if (SERVO_FACE_DEADZONE_PX < screen_error) {
        return "left";
    }
    if (screen_error < -SERVO_FACE_DEADZONE_PX) {
        return "right";
    }
    return "hold";
}
#endif

#if SERVO_INPUT_READ_ENABLED
static servo_face_input_t get_latest_servo_face_input(void)
{
    servo_face_input_t input = {};
    portENTER_CRITICAL(&s_servo_input_lock);
    input = s_latest_servo_face_input;
    portEXIT_CRITICAL(&s_servo_input_lock);
    return input;
}
#endif

#if SERVO_CONTROL_LOG_ENABLED
static void log_servo_input(const servo_face_input_t *const input, const uint64_t now_us)
{
    if (NULL == input) {
        return;
    }
    const bool center_valid = input->valid &&
                              (0 <= input->center_y) &&
                              (input->center_y < (int)PREVIEW_V_RES);
    const int center_y = center_valid ? input->center_y : SERVO_INVALID_CENTER_Y;
    const int screen_error = center_valid ? (SERVO_SCREEN_CENTER_Y - center_y) : 0;
    ESP_LOGI(TAG,
             "[servo_input] latest_seq=%lu age_ms=%llu valid=%d center_y=%d screen_error=%d",
             (unsigned long)input->sequence,
             (unsigned long long)get_servo_input_age_ms(now_us, input->completed_at_us),
             input->valid ? 1 : 0,
             center_y,
             screen_error);
}

static void log_servo_command(const servo_control_state_t *const state,
                              const servo_control_step_t *const step)
{
    if ((NULL == state) || (NULL == step)) {
        return;
    }
    ESP_LOGI(TAG,
             "[servo_cmd] applied_seq=%lu current=%ld target=%ld pulse_us=%lu direction=%s superseded=%d output=%s",
             (unsigned long)state->applied_sequence,
             (long)state->current_angle_deg,
             (long)state->target_angle_deg,
             (unsigned long)servo_angle_to_pulse_us(state->current_angle_deg),
             get_servo_direction_name(state),
             step->superseded ? 1 : 0,
             get_servo_output_mode_name());
}
#endif

static void servo_control_task(void *arg)
{
    (void)arg;

#if SERVO_CONTROL_STEP_ENABLED
    servo_control_state_t state = {};
    init_servo_control_state(&state, &s_servo_control_config);
#endif
    TickType_t last_wake_tick = xTaskGetTickCount();

    while (1) {
#if SERVO_INPUT_READ_ENABLED
        const servo_face_input_t input = get_latest_servo_face_input();
#if !SERVO_CONTROL_STEP_ENABLED
        (void)input;
#endif
#endif

#if SERVO_CONTROL_STEP_ENABLED
        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        const servo_control_step_t step = step_servo_control(&state,
                                                             &s_servo_control_config,
                                                             &input,
                                                             now_us);

#if SERVO_CONTROL_LOG_ENABLED
        if (step.input_consumed) {
            log_servo_input(&input, now_us);
        }
#endif

#if SERVO_TRACKING_OUTPUT_ENABLED
        if (step.output_changed) {
            const esp_err_t ret = servo_apply_angle(state.current_angle_deg);
            if (ESP_OK != ret) {
                ESP_LOGE(TAG, "[servo] apply angle failed: %s", esp_err_to_name(ret));
                (void)ledc_stop(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, 0);
                (void)gpio_set_level(to_gpio_num(SERVO_SAFE_LOW_GPIO), 0);
                vTaskDelete(NULL);
                return;
            }
        }
#endif

#if SERVO_CONTROL_LOG_ENABLED
        if (step.input_consumed || step.input_expired || step.output_changed || step.mode_changed) {
            log_servo_command(&state, &step);
        }
#endif

#if !SERVO_TRACKING_OUTPUT_ENABLED && !SERVO_CONTROL_LOG_ENABLED
        (void)step;
#endif
#endif

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(SERVO_UPDATE_INTERVAL_MS));
    }
}
#endif

static esp_err_t init_servo_tracking(void)
{
#if SERVO_CONTROL_TASK_ENABLED
    if (!is_servo_control_config_valid(&s_servo_control_config)) {
        ESP_LOGE(TAG, "[servo] invalid control configuration");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(init_servo_pwm_output(), TAG, "servo PWM init");

    const BaseType_t task_created = xTaskCreate(servo_control_task,
                                                "servo_control",
                                                SERVO_TASK_STACK_BYTES,
                                                NULL,
                                                SERVO_TASK_PRIORITY,
                                                &s_servo_task_handle);
    if (pdPASS != task_created) {
        ESP_LOGE(TAG, "[servo] control task creation failed");
        return ESP_ERR_NO_MEM;
    }

#if SERVO_CONTROL_LOG_ENABLED
    ESP_LOGI(TAG,
             "[servo] control_task=enabled output=%s signal_gpio=%d safe_low_gpio=%d freq=%dHz pulse_us=(%d,%d) angle_limit=(%d,%d) center=%d screen_axis=center_y screen_center=%d deadzone=%d gain=%d/%d direction_sign=%d target_step=%d apply_step=%d update_ms=%d stale_ms=%d return_ms=%d self_test=%d",
             get_servo_output_mode_name(),
             SERVO_SIGNAL_GPIO,
             SERVO_SAFE_LOW_GPIO,
             SERVO_PWM_FREQ_HZ,
             SERVO_MIN_PULSE_US,
             SERVO_MAX_PULSE_US,
             SERVO_MIN_ANGLE_DEG,
             SERVO_MAX_ANGLE_DEG,
             SERVO_CENTER_ANGLE_DEG,
             SERVO_SCREEN_CENTER_Y,
             SERVO_FACE_DEADZONE_PX,
             SERVO_TRACK_GAIN_NUM,
             SERVO_TRACK_GAIN_DEN,
             SERVO_DIRECTION_SIGN,
             SERVO_TARGET_STEP_MAX_DEG,
             SERVO_APPLY_STEP_MAX_DEG,
             SERVO_UPDATE_INTERVAL_MS,
             SERVO_INPUT_STALE_MS,
             SERVO_LOST_HOLD_MS,
             SERVO_BOOT_SELF_TEST_ENABLED);
#else
    ESP_LOGI(TAG,
             "[servo] snapshot=enabled control_task=enabled input_read=%d control_step=%d runtime_log=disabled pwm=%d tracking=%d update_ms=%d",
             SERVO_INPUT_READ_ENABLED,
             SERVO_CONTROL_STEP_ENABLED,
             SERVO_PWM_OUTPUT_ENABLED,
             SERVO_TRACKING_OUTPUT_ENABLED,
             SERVO_UPDATE_INTERVAL_MS);
#endif
    return ESP_OK;
#else
#if SERVO_INPUT_SNAPSHOT_ENABLED
    ESP_LOGI(TAG, "[servo] snapshot publisher enabled; control task disabled");
#else
    ESP_LOGI(TAG, "[servo] runtime disabled");
#endif
    return ESP_OK;
#endif
}

static void init_yuv_to_rgb_lut(void)
{
    for (int i = 0; i < 256; ++i) {
        int delta = i - 128;
        int y = i - 16;
        if (y < 0) {
            y = 0;
        }
        s_y_term_lut[i] = 298 * y + 128;
        s_u_to_b_lut[i] = 516 * delta;
        s_u_to_g_lut[i] = -100 * delta;
        s_v_to_r_lut[i] = 409 * delta;
        s_v_to_g_lut[i] = -208 * delta;
    }
}

static inline void yuv_to_rgb888(uint8_t y, uint8_t u, uint8_t v,
                                 uint8_t *r, uint8_t *g, uint8_t *b)
{
    int ri = (s_y_term_lut[y] + s_v_to_r_lut[v]) >> 8;
    int gi = (s_y_term_lut[y] + s_u_to_g_lut[u] + s_v_to_g_lut[v]) >> 8;
    int bi = (s_y_term_lut[y] + s_u_to_b_lut[u]) >> 8;
    *r = clamp_u8(ri);
    *g = clamp_u8(gi);
    *b = clamp_u8(bi);
}

static inline uint16_t yuv_to_rgb565_be(uint8_t y, uint8_t u, uint8_t v)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    yuv_to_rgb888(y, u, v, &r, &g, &b);
    uint16_t pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return (uint16_t)((pixel >> 8) | (pixel << 8));
}

static uint16_t rgb888_to_rgb565_be(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return (uint16_t)((pixel >> 8) | (pixel << 8));
}

static int get_face_center_x(const face_box_t *face)
{
    if (NULL == face) {
        return 0;
    }
    return (face->x1 + face->x2) / 2;
}

static int get_face_center_y(const face_box_t *face)
{
    if (NULL == face) {
        return 0;
    }
    return (face->y1 + face->y2) / 2;
}

static int select_nearest_face(void)
{
    if (s_face_count <= 0) {
        return FACE_TARGET_NONE;
    }

    int best_index = 0;
    int64_t best_dist_sq = INT64_MAX;
    for (int i = 0; i < s_face_count; ++i) {
        int dx = get_face_center_x(&s_faces[i]) - s_tracked_center_x;
        int dy = get_face_center_y(&s_faces[i]) - s_tracked_center_y;
        int64_t dist_sq = ((int64_t)dx * dx) + ((int64_t)dy * dy);
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best_index = i;
        }
    }

    return best_index;
}

static face_select_reason_t update_tracked_face(void)
{
    for (int i = 0; i < s_face_count; ++i) {
        s_faces[i].tracked = false;
    }

    if (s_face_count <= 0) {
        s_tracked_face_index = FACE_TARGET_NONE;
        s_tracked_center_x = 0;
        s_tracked_center_y = 0;
        s_tracked_face_valid = false;
        return FACE_SELECT_NONE;
    }

    face_select_reason_t reason = FACE_SELECT_SINGLE;
    int selected_index = 0;
    if (s_face_count > 1) {
        if (s_tracked_face_valid) {
            selected_index = select_nearest_face();
            reason = FACE_SELECT_NEAREST;
        } else {
            selected_index = (int)(esp_random() % (uint32_t)s_face_count);
            reason = FACE_SELECT_RANDOM;
        }
    }

    s_faces[selected_index].tracked = true;
    s_tracked_face_index = selected_index;
    s_tracked_center_x = get_face_center_x(&s_faces[selected_index]);
    s_tracked_center_y = get_face_center_y(&s_faces[selected_index]);
    s_tracked_face_valid = true;
    return reason;
}

static void publish_face_result(uint32_t source_sequence, uint32_t display_frame)
{
    face_result_snapshot_t result = {};
    result.face_count = s_face_count;
    result.tracked_face_index = s_tracked_face_index;
    result.tracked_center_x = s_tracked_center_x;
    result.tracked_center_y = s_tracked_center_y;
    result.tracked_face_valid = s_tracked_face_valid;
    result.source_sequence = source_sequence;
    result.display_frame = display_frame;
    if (0 < s_face_count) {
        memcpy(result.faces, s_faces, (size_t)s_face_count * sizeof(face_box_t));
    }

    portENTER_CRITICAL(&s_face_result_lock);
    s_published_face_result = result;
    portEXIT_CRITICAL(&s_face_result_lock);
}

static void update_display_face_result(void)
{
    face_result_snapshot_t result = {};
    portENTER_CRITICAL(&s_face_result_lock);
    result = s_published_face_result;
    portEXIT_CRITICAL(&s_face_result_lock);

    s_display_face_count = result.face_count;
    if (0 < s_display_face_count) {
        memcpy(s_display_faces, result.faces, (size_t)s_display_face_count * sizeof(face_box_t));
    }
    s_display_face_source_sequence = result.source_sequence;
    s_display_face_result_frame = result.display_frame;
}

static bool on_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    (void)handle;
    (void)user_data;

    portENTER_CRITICAL_ISR(&s_framebuffer_lock);
    const uint32_t completed_sequence = s_frame_count + 1U;
    s_frame_count = completed_sequence;
    if (NULL != trans && NULL != trans->buffer) {
        for (int i = 0; i < FB_COUNT; ++i) {
            if (trans->buffer == s_fb[i]) {
                s_fb_completed_sequence[i] = completed_sequence;
                s_latest_completed_fb = i;
                break;
            }
        }
    }
    portEXIT_CRITICAL_ISR(&s_framebuffer_lock);

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_frame_ready, &woken);
    return (woken == pdTRUE);
}

static bool on_get_new_trans(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    (void)handle;
    (void)user_data;

    portENTER_CRITICAL_ISR(&s_framebuffer_lock);
    int next = (s_active_fb + 1) % FB_COUNT;
    if (next == s_locked_fb) {
        next = (next + 1) % FB_COUNT;
    }
    trans->buffer = s_fb[next];
    trans->buflen = CAM_H_RES * CAM_V_RES * 2;
    s_active_fb = next;
    portEXIT_CRITICAL_ISR(&s_framebuffer_lock);
    return false;
}

static void vyuy_subsample_to_rgb565_chunk(const uint8_t *yuv, uint16_t *rgb,
                                           uint32_t dst_y_start, uint32_t chunk_lines)
{
    const uint32_t src_stride = CAM_H_RES * 2U;
    const uint32_t src_x_byte_off = dst_y_start * 4U;

    /*
     * Keep the verified-correct preview mapping:
     * display(x, y) <- source(x = y * 2, y = x * 2), pixel format VYUY.
     *
     * Iterating per output column makes each inner loop read contiguous bytes
     * from one source line, which is friendlier to PSRAM/cache than the old
     * per-pixel full-frame conversion order.
     */
    for (uint32_t dx = 0; dx < PREVIEW_H_RES; ++dx) {
        const uint8_t *src = yuv + (dx * 2U) * src_stride + src_x_byte_off;
        uint16_t *dst = rgb + dx;
        for (uint32_t row = 0; row < chunk_lines; ++row) {
            dst[row * PREVIEW_H_RES] = yuv_to_rgb565_be(src[1], src[2], src[0]);
            src += 4;
        }
    }
}

static void fill_fixed_color_bars_chunk(uint16_t *rgb, uint32_t chunk_lines)
{
    static const uint16_t color_bars[] = {
        0xffff, 0xffe0, 0x07ff, 0x07e0,
        0xf81f, 0xf800, 0x001f, 0x0000,
    };
    const uint32_t color_bar_count = sizeof(color_bars) / sizeof(color_bars[0]);

    for (uint32_t row = 0; row < chunk_lines; ++row) {
        uint16_t *dst = rgb + (row * PREVIEW_H_RES);
        for (uint32_t x = 0; x < PREVIEW_H_RES; ++x) {
            dst[x] = color_bars[(x * color_bar_count) / PREVIEW_H_RES];
        }
    }
}

static inline uint16_t rgb565_be_swap_red_blue(uint16_t stored)
{
    uint16_t pixel = (uint16_t)((stored >> 8) | (stored << 8));
    uint16_t swapped = (uint16_t)(((pixel & 0x001fU) << 11) |
                                  (pixel & 0x07e0U) |
                                  ((pixel & 0xf800U) >> 11));
    return (uint16_t)((swapped >> 8) | (swapped << 8));
}

static void prepare_face_input_from_framebuffer(const uint8_t *framebuffer,
                                                face_input_variant_t variant,
                                                const face_scan_roi_t *roi)
{
    const uint32_t dst_w = get_face_input_width(variant);
    const uint32_t dst_h = get_face_input_height(variant);
    const bool landscape = face_input_variant_is_landscape(variant);
    const bool bgr_order = face_input_variant_uses_bgr(variant);
    const uint32_t src_stride = CAM_H_RES * 2U;

    if (NULL == framebuffer || NULL == s_face_rgb565 || NULL == roi) {
        return;
    }

    if (!landscape) {
        for (uint32_t dy = 0; dy < dst_h; ++dy) {
            uint32_t preview_y = (uint32_t)roi->y + ((dy * (uint32_t)roi->h) / dst_h);
            preview_y = std::min(preview_y, (uint32_t)(PREVIEW_V_RES - 1));
            uint16_t *dst_row = s_face_rgb565 + (dy * dst_w);
            for (uint32_t dx = 0; dx < dst_w; ++dx) {
                uint32_t preview_x = (uint32_t)roi->x + ((dx * (uint32_t)roi->w) / dst_w);
                preview_x = (uint32_t)clamp_int((int)preview_x - roi->shift_x,
                                                0,
                                                (int)PREVIEW_H_RES - 1);
                const uint8_t *src = framebuffer +
                                     (preview_x * 2U) * src_stride + preview_y * 4U;
                uint16_t stored = yuv_to_rgb565_be(src[1], src[2], src[0]);
                dst_row[dx] = bgr_order ? rgb565_be_swap_red_blue(stored) : stored;
            }
        }
        return;
    }

    for (uint32_t dy = 0; dy < dst_h; ++dy) {
        uint32_t preview_x = (uint32_t)roi->x + ((dy * (uint32_t)roi->w) / dst_h);
        preview_x = (uint32_t)clamp_int((int)preview_x - roi->shift_x,
                                        0,
                                        (int)PREVIEW_H_RES - 1);
        uint16_t *dst_row = s_face_rgb565 + (dy * dst_w);
        for (uint32_t dx = 0; dx < dst_w; ++dx) {
            uint32_t preview_y = (uint32_t)roi->y + ((dx * (uint32_t)roi->h) / dst_w);
            preview_y = std::min(preview_y, (uint32_t)(PREVIEW_V_RES - 1));
            const uint8_t *src = framebuffer +
                                 (preview_x * 2U) * src_stride + preview_y * 4U;
            const uint16_t stored = yuv_to_rgb565_be(src[1], src[2], src[0]);
            dst_row[dx] = bgr_order ? rgb565_be_swap_red_blue(stored) : stored;
        }
    }
}

static bool should_log_face_input_diag(face_input_variant_t variant)
{
    uint32_t attempts = s_face_input_attempts[face_input_variant_index(variant)];
    return (attempts <= FACE_INPUT_DIAG_FIRST_COUNT) || ((attempts % FACE_INPUT_DIAG_INTERVAL) == 0U);
}

static void log_face_input_diag(uint32_t display_frame,
                                face_input_variant_t variant,
                                face_scan_window_t scan,
                                const face_scan_roi_t *roi)
{
    if (NULL == roi) {
        return;
    }

    const uint16_t input_w = get_face_input_width(variant);
    const uint16_t input_h = get_face_input_height(variant);
    const bool bgr_order = face_input_variant_uses_bgr(variant);
    uint32_t sum_r = 0;
    uint32_t sum_g = 0;
    uint32_t sum_b = 0;
    uint32_t sum_luma = 0;
    uint8_t min_luma = 255;
    uint8_t max_luma = 0;

    for (int gy = 0; gy < FACE_INPUT_DIAG_GRID_ROWS; ++gy) {
        int y = ((gy * 2 + 1) * input_h) / (FACE_INPUT_DIAG_GRID_ROWS * 2);
        y = clamp_int(y, 0, input_h - 1);
        for (int gx = 0; gx < FACE_INPUT_DIAG_GRID_COLS; ++gx) {
            int x = ((gx * 2 + 1) * input_w) / (FACE_INPUT_DIAG_GRID_COLS * 2);
            x = clamp_int(x, 0, input_w - 1);
            uint16_t stored = s_face_rgb565[y * input_w + x];
            uint16_t pixel = (uint16_t)((stored >> 8) | (stored << 8));
            uint8_t channel_1 = (uint8_t)(((pixel >> 11) & 0x1fU) << 3);
            uint8_t g = (uint8_t)(((pixel >> 5) & 0x3fU) << 2);
            uint8_t channel_3 = (uint8_t)((pixel & 0x1fU) << 3);
            uint8_t r = bgr_order ? channel_3 : channel_1;
            uint8_t b = bgr_order ? channel_1 : channel_3;
            uint8_t luma = (uint8_t)((77U * r + 150U * g + 29U * b) >> 8);
            sum_r += r;
            sum_g += g;
            sum_b += b;
            sum_luma += luma;
            if (luma < min_luma) {
                min_luma = luma;
            }
            if (luma > max_luma) {
                max_luma = luma;
            }
        }
    }

    const uint32_t sample_count = FACE_INPUT_DIAG_GRID_COLS * FACE_INPUT_DIAG_GRID_ROWS;
    uint16_t center_stored = s_face_rgb565[((input_h / 2) * input_w) + (input_w / 2)];
    uint16_t center_rgb565 = (uint16_t)((center_stored >> 8) | (center_stored << 8));
    ESP_LOGI(TAG,
             "[detect_input] frame=%lu variant=%s locked=%d scan=%s roi=(%d,%d,%d,%d) shift_x=%d size=%ux%u pixfmt=BGR/RGB565_BE avg_rgb=(%lu,%lu,%lu) luma_avg=%lu luma_range=(%u,%u) center_rgb565=0x%04x attempts=%lu hits=%lu scan_attempts=%lu scan_hits=%lu",
             (unsigned long)display_frame,
             face_input_variant_name(variant),
             s_face_input_variant_locked ? 1 : 0,
             face_scan_window_name(scan),
             roi->x,
             roi->y,
             roi->w,
             roi->h,
             roi->shift_x,
             (unsigned)input_w,
             (unsigned)input_h,
             (unsigned long)(sum_r / sample_count),
             (unsigned long)(sum_g / sample_count),
             (unsigned long)(sum_b / sample_count),
             (unsigned long)(sum_luma / sample_count),
             min_luma,
             max_luma,
             center_rgb565,
             (unsigned long)s_face_input_attempts[face_input_variant_index(variant)],
             (unsigned long)s_face_input_hits[face_input_variant_index(variant)],
             (unsigned long)s_face_scan_attempts[face_scan_window_index(scan)],
             (unsigned long)s_face_scan_hits[face_scan_window_index(scan)]);
}

static esp_err_t wait_lcd_draw_completion(lcd_pending_draw_t *pending,
                                          uint64_t *draw_us)
{
    if (NULL == pending || NULL == draw_us) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pending->active) {
        return ESP_OK;
    }

    esp_err_t ret = display_hal_wait_pending(LCD_DRAW_WAIT_TIMEOUT_MS);
    if (ESP_OK != ret) {
        return ret;
    }

    *draw_us += (uint64_t)esp_timer_get_time() - pending->started_us;
    pending->active = false;
    return ESP_OK;
}

static esp_err_t draw_solid_overlay_rect(int x, int y, int width, int height,
                                         uint16_t color, uint64_t *draw_us)
{
    if (NULL == draw_us || NULL == s_rgb_chunk_buf[0] || width <= 0 || height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int x1 = clamp_int(x, 0, PREVIEW_H_RES - 1);
    const int y1 = clamp_int(y, 0, PREVIEW_V_RES - 1);
    const int x2 = clamp_int(x + width - 1, 0, PREVIEW_H_RES - 1);
    const int y2 = clamp_int(y + height - 1, 0, PREVIEW_V_RES - 1);
    if (x2 < x1 || y2 < y1) {
        return ESP_OK;
    }

    const int clipped_width = x2 - x1 + 1;
    const int clipped_height = y2 - y1 + 1;
    const size_t pixel_count = (size_t)clipped_width * (size_t)clipped_height;
    const size_t buffer_capacity = (size_t)PREVIEW_H_RES * (size_t)LCD_CHUNK_LINES;
    if (pixel_count > buffer_capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    std::fill_n(s_rgb_chunk_buf[0], pixel_count, color);
    const uint64_t started_us = (uint64_t)esp_timer_get_time();
    esp_err_t ret = display_hal_draw_bitmap_rgb565(PREVIEW_X_OFFSET + x1,
                                                    PREVIEW_Y_OFFSET + y1,
                                                    clipped_width,
                                                    clipped_height,
                                                    s_rgb_chunk_buf[0]);
    if (ESP_OK != ret) {
        return ret;
    }
    ret = display_hal_wait_pending(LCD_DRAW_WAIT_TIMEOUT_MS);
    if (ESP_OK == ret) {
        *draw_us += (uint64_t)esp_timer_get_time() - started_us;
    }
    return ret;
}

static esp_err_t draw_face_boxes_overlay(uint64_t *draw_us)
{
    if (NULL == draw_us) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t target_color = rgb888_to_rgb565_be(FACE_TARGET_COLOR_R,
                                                       FACE_TARGET_COLOR_G,
                                                       FACE_TARGET_COLOR_B);
    const uint16_t other_color = rgb888_to_rgb565_be(FACE_OTHER_COLOR_R,
                                                      FACE_OTHER_COLOR_G,
                                                      FACE_OTHER_COLOR_B);

    for (int i = 0; i < s_display_face_count; ++i) {
        const face_box_t *face = &s_display_faces[i];
        const int x1 = clamp_int(face->x1, 0, PREVIEW_H_RES - 1);
        const int y1 = clamp_int(face->y1, 0, PREVIEW_V_RES - 1);
        const int x2 = clamp_int(face->x2, 0, PREVIEW_H_RES - 1);
        const int y2 = clamp_int(face->y2, 0, PREVIEW_V_RES - 1);
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        const uint16_t color = face->tracked ? target_color : other_color;
        const int box_width = x2 - x1 + 1;
        const int box_height = y2 - y1 + 1;
        ESP_RETURN_ON_ERROR(draw_solid_overlay_rect(x1, y1, box_width,
                                                    FACE_BOX_THICKNESS, color, draw_us),
                            TAG, "draw face top edge failed");
        ESP_RETURN_ON_ERROR(draw_solid_overlay_rect(x1, y2 - FACE_BOX_THICKNESS + 1,
                                                    box_width, FACE_BOX_THICKNESS,
                                                    color, draw_us),
                            TAG, "draw face bottom edge failed");
        ESP_RETURN_ON_ERROR(draw_solid_overlay_rect(x1, y1, FACE_BOX_THICKNESS,
                                                    box_height, color, draw_us),
                            TAG, "draw face left edge failed");
        ESP_RETURN_ON_ERROR(draw_solid_overlay_rect(x2 - FACE_BOX_THICKNESS + 1, y1,
                                                    FACE_BOX_THICKNESS, box_height,
                                                    color, draw_us),
                            TAG, "draw face right edge failed");
    }
    return ESP_OK;
}

static int dvp_check_signal(gpio_num_t pin, const char *name)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    int changes = 0;
    int last = gpio_get_level(pin);
    for (int i = 0; i < CAMERA_DIAG_SAMPLE_COUNT; i++) {
        int cur = gpio_get_level(pin);
        if (cur != last) {
            changes++;
            last = cur;
        }
    }
    ESP_LOGI(TAG, "  %s (GPIO%d): %d transitions / %d samples", name, pin, changes, CAMERA_DIAG_SAMPLE_COUNT);
    return changes;
}

static esp_err_t init_face_detector(void)
{
    const size_t face_rgb_size = PREVIEW_H_RES * PREVIEW_V_RES * sizeof(uint16_t);
    s_face_rgb565 = (uint16_t *)heap_caps_aligned_alloc(64, face_rgb_size,
                                                        MALLOC_CAP_SPIRAM);
    if (NULL == s_face_rgb565) {
        ESP_LOGE(TAG, "face RGB565 PSRAM buffer alloc failed: size=%u", (unsigned)face_rgb_size);
        return ESP_ERR_NO_MEM;
    }
    memset(s_face_rgb565, 0, face_rgb_size);

    s_face_detect_queue = xQueueCreate(1, sizeof(face_detect_request_t));
    s_face_rgb_available = xSemaphoreCreateBinary();
    if (NULL == s_face_detect_queue || NULL == s_face_rgb_available) {
        ESP_LOGE(TAG, "face detection synchronization alloc failed");
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_face_rgb_available);

    flash_param_face_model_memory_t model_memory = {};
    s_face_model = create_flash_param_face_detector(FACE_DETECT_SCORE_THR,
                                                     FACE_DETECT_NMS_THR,
                                                     &model_memory);
    if (NULL == s_face_model) {
        ESP_LOGE(TAG, "flash-backed face detector alloc failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "human face detector ready, input variants=%ux%u/%ux%u RGB565_BE source",
             (unsigned)PREVIEW_H_RES,
             (unsigned)PREVIEW_V_RES,
             (unsigned)PREVIEW_V_RES,
             (unsigned)PREVIEW_H_RES);
    ESP_LOGI(TAG, "  face_rgb=%u bytes, raw_yuv_snapshot=off, face_input_memory=psram, detect interval=%d display frames, score_thr=%.2f, initial_variant=%s auto_lock=1",
             (unsigned)face_rgb_size,
             FACE_DETECT_INTERVAL_FRAMES,
             (double)FACE_DETECT_SCORE_THR,
             face_input_variant_name(s_face_input_variant));
    ESP_LOGI(TAG,
             "  model_parameters=flash_direct param_copy=off tensor_internal_limit_per_model=%u model_ram_internal=%u model_ram_psram=%u",
             (unsigned)model_memory.internal_limit_per_model_bytes,
             (unsigned)model_memory.internal_bytes,
             (unsigned)model_memory.psram_bytes);
    return ESP_OK;
}

static void map_face_box_to_preview(face_input_variant_t variant,
                                    const std::vector<int> &box,
                                    int *out_x1,
                                    int *out_y1,
                                    int *out_x2,
                                    int *out_y2)
{
    if (face_input_variant_is_landscape(variant)) {
        *out_x1 = clamp_int(box[1], 0, PREVIEW_H_RES - 1);
        *out_y1 = clamp_int(box[0], 0, PREVIEW_V_RES - 1);
        *out_x2 = clamp_int(box[3], 0, PREVIEW_H_RES - 1);
        *out_y2 = clamp_int(box[2], 0, PREVIEW_V_RES - 1);
    } else {
        *out_x1 = clamp_int(box[0], 0, PREVIEW_H_RES - 1);
        *out_y1 = clamp_int(box[1], 0, PREVIEW_V_RES - 1);
        *out_x2 = clamp_int(box[2], 0, PREVIEW_H_RES - 1);
        *out_y2 = clamp_int(box[3], 0, PREVIEW_V_RES - 1);
    }
}

static void map_face_box_to_preview_roi(face_input_variant_t variant,
                                        const std::vector<int> &box,
                                        const face_scan_roi_t *roi,
                                        int *out_x1,
                                        int *out_y1,
                                        int *out_x2,
                                        int *out_y2)
{
    int virtual_x1 = 0;
    int virtual_y1 = 0;
    int virtual_x2 = 0;
    int virtual_y2 = 0;

    if (NULL == roi || NULL == out_x1 || NULL == out_y1 || NULL == out_x2 || NULL == out_y2) {
        return;
    }

    map_face_box_to_preview(variant, box, &virtual_x1, &virtual_y1, &virtual_x2, &virtual_y2);
    *out_x1 = map_axis_from_full_to_roi(virtual_x1, (int)PREVIEW_H_RES, roi->x, roi->w);
    *out_y1 = map_axis_from_full_to_roi(virtual_y1, (int)PREVIEW_V_RES, roi->y, roi->h);
    *out_x2 = map_axis_from_full_to_roi(virtual_x2, (int)PREVIEW_H_RES, roi->x, roi->w);
    *out_y2 = map_axis_from_full_to_roi(virtual_y2, (int)PREVIEW_V_RES, roi->y, roi->h);
    *out_x1 = clamp_int(*out_x1 - roi->shift_x, 0, (int)PREVIEW_H_RES - 1);
    *out_x2 = clamp_int(*out_x2 - roi->shift_x, 0, (int)PREVIEW_H_RES - 1);
}

static face_select_reason_t update_face_boxes_from_result(const std::list<dl::detect::result_t> &results,
                                                          face_input_variant_t variant,
                                                          const face_scan_roi_t *roi)
{
    int count = 0;
    for (const auto &result : results) {
        if (count >= FACE_MAX_BOXES) {
            break;
        }
        if (result.box.size() < 4) {
            continue;
        }

        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
        map_face_box_to_preview_roi(variant, result.box, roi, &x1, &y1, &x2, &y2);
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        s_faces[count] = {
            .x1 = x1,
            .y1 = y1,
            .x2 = x2,
            .y2 = y2,
            .score = result.score,
            .tracked = false,
        };
        count++;
    }

    s_face_count = count;
    return update_tracked_face();
}

static void log_face_detection_result(uint32_t display_frame,
                                      uint64_t detect_us,
                                      face_select_reason_t reason,
                                      face_input_variant_t variant,
                                      face_scan_window_t scan,
                                      const face_scan_roi_t *roi)
{
    if (!FACE_RUNTIME_VERBOSE_LOG_ENABLED || NULL == roi) {
        return;
    }

    int target_dx = 0;
    int target_dy = 0;
    if (s_tracked_face_index != FACE_TARGET_NONE) {
        target_dx = s_tracked_center_x - ((int)PREVIEW_H_RES / 2);
        target_dy = s_tracked_center_y - ((int)PREVIEW_V_RES / 2);
    }

    ESP_LOGI(TAG,
             "[face] frame=%lu variant=%s locked=%d scan=%s roi=(%d,%d,%d,%d) shift_x=%d count=%d detect_ms=%llu target=%d reason=%s target_center=(%d,%d) target_offset=(%d,%d)",
             (unsigned long)display_frame,
             face_input_variant_name(variant),
             s_face_input_variant_locked ? 1 : 0,
             face_scan_window_name(scan),
             roi->x,
             roi->y,
             roi->w,
             roi->h,
             roi->shift_x,
             s_face_count,
             (unsigned long long)(detect_us / 1000U),
             s_tracked_face_index,
             face_select_reason_name(reason),
             s_tracked_center_x,
             s_tracked_center_y,
             target_dx,
             target_dy);

    for (int i = 0; i < s_face_count; ++i) {
        ESP_LOGI(TAG,
                 "[face_box] frame=%lu scan=%s idx=%d tracked=%d score=%.3f box=(%d,%d,%d,%d) center=(%d,%d)",
                 (unsigned long)display_frame,
                 face_scan_window_name(scan),
                 i,
                 s_faces[i].tracked ? 1 : 0,
                 (double)s_faces[i].score,
                 s_faces[i].x1,
                 s_faces[i].y1,
                 s_faces[i].x2,
                 s_faces[i].y2,
                 get_face_center_x(&s_faces[i]),
                 get_face_center_y(&s_faces[i]));
    }
}

static void rotate_face_input_variant_after_miss(face_input_variant_t variant)
{
    if (s_face_input_variant_locked) {
        return;
    }
    int next_variant = (face_input_variant_index(variant) + 1) % FACE_INPUT_VARIANT_COUNT;
    s_face_input_variant = (face_input_variant_t)next_variant;
    if (FACE_RUNTIME_VERBOSE_LOG_ENABLED) {
        ESP_LOGI(TAG, "[detect_input] no face with variant=%s, next_variant=%s",
                 face_input_variant_name(variant), face_input_variant_name(s_face_input_variant));
    }
}

static face_select_reason_t run_face_scan(uint32_t display_frame,
                                          face_input_variant_t variant,
                                          face_scan_window_t scan,
                                          const face_scan_roi_t *roi)
{
    if (NULL == s_face_rgb565 || NULL == roi) {
        s_face_count = 0;
        return FACE_SELECT_NONE;
    }

    s_face_scan_attempts[face_scan_window_index(scan)]++;
    if (FACE_INPUT_DIAG_ENABLED && FACE_SCAN_FULL == scan && should_log_face_input_diag(variant)) {
        log_face_input_diag(display_frame, variant, scan, roi);
    }

    dl::image::img_t img = {};
    img.data = s_face_rgb565;
    img.width = get_face_input_width(variant);
    img.height = get_face_input_height(variant);
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

    const std::list<dl::detect::result_t> &results = s_face_model->run(img);
    face_select_reason_t reason = update_face_boxes_from_result(results, variant, roi);
    if (s_face_count > 0) {
        s_face_scan_hits[face_scan_window_index(scan)]++;
    }

    return reason;
}

static esp_err_t run_face_detect_job(const face_detect_request_t *request)
{
    if (NULL == request || NULL == request->framebuffer ||
        NULL == s_face_model || NULL == s_face_rgb565) {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t job_start_us = (uint64_t)esp_timer_get_time();
    face_input_variant_t variant = request->variant;
    bool had_previous_face = s_tracked_face_valid;
    int previous_center_x = s_tracked_center_x;
    int previous_center_y = s_tracked_center_y;
    face_scan_window_t scan = request->scan;
    face_scan_roi_t roi = request->roi;

    uint64_t prepare_start_us = (uint64_t)esp_timer_get_time();
    prepare_face_input_from_framebuffer(request->framebuffer, variant, &roi);
    uint64_t prepare_us = (uint64_t)esp_timer_get_time() - prepare_start_us;

    uint64_t start_us = (uint64_t)esp_timer_get_time();
    s_face_input_attempts[face_input_variant_index(variant)]++;

    face_select_reason_t reason = run_face_scan(request->display_frame, variant, scan, &roi);
#if SERVO_INPUT_SNAPSHOT_ENABLED
    publish_servo_face_input((0 < s_face_count) && s_tracked_face_valid,
                             s_tracked_face_valid ? s_tracked_center_y : SERVO_INVALID_CENTER_Y);
#endif
    if (0 < s_face_count) {
        s_next_face_scan = FACE_SCAN_FULL;
    } else if (FACE_SCAN_FULL == scan) {
        s_next_face_scan = choose_edge_scan(had_previous_face, previous_center_x);
        if (had_previous_face) {
            s_tracked_face_valid = true;
            s_tracked_center_x = previous_center_x;
            s_tracked_center_y = previous_center_y;
        }
        if (FACE_RUNTIME_VERBOSE_LOG_ENABLED) {
            ESP_LOGI(TAG,
                     "[face_scan] frame=%lu full_miss=1 next_scan=%s current_job_inferences=1",
                     (unsigned long)request->display_frame,
                     face_scan_window_name(s_next_face_scan));
        }
    } else {
        s_next_face_scan = FACE_SCAN_FULL;
    }

    uint64_t elapsed_us = esp_timer_get_time() - start_us;
    if (s_face_count > 0) {
        s_face_input_hits[face_input_variant_index(variant)]++;
        if (!s_face_input_variant_locked) {
            s_face_input_variant_locked = true;
            ESP_LOGI(TAG, "[detect_input] lock variant=%s after first face hit", face_input_variant_name(variant));
        }
    } else if (FACE_SCAN_FULL != scan) {
        rotate_face_input_variant_after_miss(variant);
    }

    portENTER_CRITICAL(&s_detect_metrics_lock);
    s_last_detect_us = elapsed_us;
    s_window_detect_us += elapsed_us;
    s_detect_prepare_us += prepare_us;
    s_detect_frame_count++;
    uint32_t request_count = s_detect_request_count;
    uint32_t request_drop_count = s_detect_request_drop_count;
    portEXIT_CRITICAL(&s_detect_metrics_lock);

    log_face_detection_result(request->display_frame, elapsed_us, reason, variant, scan, &roi);
    if ((0 < s_face_count) || (FACE_SCAN_FULL != scan) || !had_previous_face) {
        publish_face_result(request->source_sequence, request->display_frame);
    }
    uint64_t queue_wait_us = job_start_us - request->ready_us;
    if (FACE_RUNTIME_VERBOSE_LOG_ENABLED) {
        ESP_LOGI(TAG,
                 "[detect_job] source_seq=%lu frame=%lu scan=%s wait_ms=%llu prep_ms=%llu detect_ms=%llu next_scan=%s requests=%lu drops=%lu",
                 (unsigned long)request->source_sequence,
                 (unsigned long)request->display_frame,
                 face_scan_window_name(scan),
                 (unsigned long long)(queue_wait_us / 1000U),
                 (unsigned long long)(prepare_us / 1000U),
                 (unsigned long long)(elapsed_us / 1000U),
                 face_scan_window_name(s_next_face_scan),
                 (unsigned long)request_count,
                 (unsigned long)request_drop_count);
    }
    return ESP_OK;
}

static bool should_log_capture_gate(uint32_t cycle)
{
    return (cycle <= CAPTURE_GATE_DIAG_FIRST_COUNT) ||
           ((cycle % CAPTURE_GATE_DIAG_INTERVAL) == 0U);
}

static esp_err_t pause_capture_for_detection(uint32_t *cycle, uint64_t *started_us)
{
    if (NULL == cycle || NULL == started_us || NULL == s_cam_ctlr || s_capture_paused) {
        return ESP_ERR_INVALID_STATE;
    }

    *cycle = 0;
    *started_us = 0;
    uint64_t pause_started_us = (uint64_t)esp_timer_get_time();
    esp_err_t stop_ret = esp_cam_ctlr_stop(s_cam_ctlr);
    esp_err_t disable_ret = ESP_ERR_INVALID_STATE;
    if (ESP_OK == stop_ret) {
        disable_ret = esp_cam_ctlr_disable(s_cam_ctlr);
    }

    portENTER_CRITICAL(&s_detect_metrics_lock);
    s_capture_pause_count++;
    *cycle = s_capture_pause_count;
    if ((ESP_OK != stop_ret) || (ESP_OK != disable_ret)) {
        s_capture_pause_error_count++;
    }
    portEXIT_CRITICAL(&s_detect_metrics_lock);

    if ((ESP_OK != stop_ret) || (ESP_OK != disable_ret)) {
        ESP_LOGE(TAG,
                 "[capture_gate] pause cycle=%lu stop=%s disable=%s",
                 (unsigned long)*cycle,
                 esp_err_to_name(stop_ret),
                 esp_err_to_name(disable_ret));
        if (ESP_OK == stop_ret) {
            esp_err_t enable_ret = esp_cam_ctlr_enable(s_cam_ctlr);
            esp_err_t start_ret = (ESP_OK == enable_ret) ? esp_cam_ctlr_start(s_cam_ctlr) : ESP_ERR_INVALID_STATE;
            ESP_LOGE(TAG,
                     "[capture_gate] pause_rollback cycle=%lu enable=%s start=%s",
                     (unsigned long)*cycle,
                     esp_err_to_name(enable_ret),
                     esp_err_to_name(start_ret));
        }
        return (ESP_OK != stop_ret) ? stop_ret : disable_ret;
    }

    s_capture_paused = true;
    *started_us = pause_started_us;
    while (pdTRUE == xSemaphoreTake(s_frame_ready, 0)) {
    }
    if (should_log_capture_gate(*cycle)) {
        ESP_LOGI(TAG,
                 "[capture_gate] paused cycle=%lu frame_seq=%lu",
                 (unsigned long)*cycle,
                 (unsigned long)s_frame_count);
    }
    return ESP_OK;
}

static esp_err_t resume_capture_after_detection(const face_detect_request_t *request)
{
    if (NULL == request || NULL == s_cam_ctlr || !s_capture_paused) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t enable_ret = esp_cam_ctlr_enable(s_cam_ctlr);
    esp_err_t start_ret = ESP_ERR_INVALID_STATE;
    if (ESP_OK == enable_ret) {
        start_ret = esp_cam_ctlr_start(s_cam_ctlr);
    }
    uint64_t paused_us = (uint64_t)esp_timer_get_time() - request->capture_pause_started_us;

    portENTER_CRITICAL(&s_detect_metrics_lock);
    s_capture_pause_us += paused_us;
    if ((ESP_OK != enable_ret) || (ESP_OK != start_ret)) {
        s_capture_pause_error_count++;
    }
    portEXIT_CRITICAL(&s_detect_metrics_lock);

    if ((ESP_OK != enable_ret) || (ESP_OK != start_ret)) {
        ESP_LOGE(TAG,
                 "[capture_gate] resume cycle=%lu enable=%s start=%s paused_ms=%llu",
                 (unsigned long)request->capture_pause_cycle,
                 esp_err_to_name(enable_ret),
                 esp_err_to_name(start_ret),
                 (unsigned long long)(paused_us / 1000U));
        return (ESP_OK != enable_ret) ? enable_ret : start_ret;
    }

    s_capture_paused = false;
    if (should_log_capture_gate(request->capture_pause_cycle)) {
        ESP_LOGI(TAG,
                 "[capture_gate] resumed cycle=%lu paused_ms=%llu frame_seq=%lu",
                 (unsigned long)request->capture_pause_cycle,
                 (unsigned long long)(paused_us / 1000U),
                 (unsigned long)s_frame_count);
    }
    return ESP_OK;
}

static bool begin_face_detection(uint32_t display_frame,
                                 uint32_t source_sequence,
                                 face_detect_request_t *request)
{
    if (NULL == request || NULL == s_face_rgb565 || NULL == s_face_detect_queue ||
        NULL == s_face_rgb_available) {
        return false;
    }
    if (pdTRUE != xSemaphoreTake(s_face_rgb_available, 0)) {
        portENTER_CRITICAL(&s_detect_metrics_lock);
        s_detect_request_drop_count++;
        portEXIT_CRITICAL(&s_detect_metrics_lock);
        return false;
    }

    face_input_variant_t variant = s_face_input_variant;
    face_scan_window_t scan = s_next_face_scan;
    face_scan_roi_t roi = make_face_scan_roi(scan);
    *request = {
        .display_frame = display_frame,
        .source_sequence = source_sequence,
        .ready_us = 0,
        .capture_pause_started_us = 0,
        .capture_pause_cycle = 0,
        .framebuffer = NULL,
        .variant = variant,
        .scan = scan,
        .roi = roi,
    };
    return true;
}

static bool submit_face_detection(face_detect_request_t *request)
{
    if (NULL == request || NULL == s_face_rgb565 || NULL == s_face_detect_queue ||
        NULL == s_face_rgb_available) {
        return false;
    }

    request->ready_us = (uint64_t)esp_timer_get_time();

    if (pdTRUE != xQueueSend(s_face_detect_queue, request, 0)) {
        xSemaphoreGive(s_face_rgb_available);
        portENTER_CRITICAL(&s_detect_metrics_lock);
        s_detect_request_drop_count++;
        portEXIT_CRITICAL(&s_detect_metrics_lock);
        return false;
    }

    portENTER_CRITICAL(&s_detect_metrics_lock);
    s_detect_request_count++;
    portEXIT_CRITICAL(&s_detect_metrics_lock);
    return true;
}

static void face_detect_task(void *arg)
{
    (void)arg;

    while (1) {
        face_detect_request_t request = {};
        if (pdTRUE != xQueueReceive(s_face_detect_queue, &request, portMAX_DELAY)) {
            continue;
        }

        esp_err_t ret = run_face_detect_job(&request);

        if (ESP_OK != ret) {
            portENTER_CRITICAL(&s_detect_metrics_lock);
            s_detect_error_count++;
            portEXIT_CRITICAL(&s_detect_metrics_lock);
            s_face_count = 0;
            s_tracked_face_index = FACE_TARGET_NONE;
            s_tracked_face_valid = false;
#if SERVO_INPUT_SNAPSHOT_ENABLED
            publish_servo_face_input(false, SERVO_INVALID_CENTER_Y);
#endif
            publish_face_result(request.source_sequence, request.display_frame);
            ESP_LOGE(TAG, "[detect_job] failed: %s", esp_err_to_name(ret));
        }
        esp_err_t resume_ret = resume_capture_after_detection(&request);
        if (ESP_OK != resume_ret) {
            ESP_LOGE(TAG, "[detect_job] capture resume failed: %s",
                     esp_err_to_name(resume_ret));
        }
        xSemaphoreGive(s_face_rgb_available);
    }
}

static esp_err_t start_face_detect_task(void)
{
    BaseType_t task_created = xTaskCreatePinnedToCore(face_detect_task,
                                                      "face_detect",
                                                      FACE_DETECT_TASK_STACK_BYTES,
                                                      NULL,
                                                      FACE_DETECT_TASK_PRIORITY,
                                                      &s_face_detect_task_handle,
                                                      FACE_DETECT_TASK_CORE);
    if (pdPASS != task_created) {
        ESP_LOGE(TAG, "face detection task creation failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "  face task started: core=%d priority=%d stack=%d bytes, max_inferences_per_job=1",
             FACE_DETECT_TASK_CORE,
             FACE_DETECT_TASK_PRIORITY,
             FACE_DETECT_TASK_STACK_BYTES);
    ESP_LOGI(TAG,
             "  face_input_handoff=locked_framebuffer_read_only producer_core=%d consumer_core=%d",
             xPortGetCoreID(),
             FACE_DETECT_TASK_CORE);
    return ESP_OK;
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Camera Face Detect Demo ===");
    ESP_LOGI(TAG, "Camera: SP0A39 %dx%d VYUY DVP", CAM_H_RES, CAM_V_RES);
    ESP_LOGI(TAG, "Display: ST7789V3 %dx%d RGB565, preview %dx%d",
             LCD_H_RES, LCD_V_RES, PREVIEW_H_RES, PREVIEW_V_RES);
    ESP_LOGI(TAG,
             "Camera control: SCCB=0x%02x RESET=IOEX P%d_%d PWDN=IOEX P%d_%d external_mclk=%dHz",
             BOARD_LAIWFS300_SP0A39_I2C_ADDR_7BIT,
             BOARD_LAIWFS300_IOEX_CAMERA_RESET_PORT,
             BOARD_LAIWFS300_IOEX_CAMERA_RESET_PIN,
             BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PORT,
             BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PIN,
             BOARD_LAIWFS300_CAMERA_EXTERNAL_MCLK_HZ);
    ESP_LOGI(TAG, "Expected log markers: [face], [face_box], [face_scan], [detect_job], [preview]");
    ESP_LOGI(TAG, "Waiting %d ms for serial monitor...", SERIAL_MONITOR_WAIT_MS);
    vTaskDelay(pdMS_TO_TICKS(SERIAL_MONITOR_WAIT_MS));

    esp_err_t ret = board_laiwfs300_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "--- Phase 0: Servo Tracking Init ---");
    ret = init_servo_tracking();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "servo tracking disabled: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "--- Phase 1: Display Init ---");
    ret = board_laiwfs300_display_init_with_config(LCD_PREVIEW_PIXEL_CLOCK_HZ, LCD_CHUNK_LINES);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "display init at %d Hz failed, fallback to default: %s",
                 LCD_PREVIEW_PIXEL_CLOCK_HZ, esp_err_to_name(ret));
        ret = board_laiwfs300_display_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "display init FAILED: %s", esp_err_to_name(ret));
        return;
    }
    display_hal_fill_rgb565(0x0000);
    ESP_LOGI(TAG, "  display ready");

    ESP_LOGI(TAG, "--- Phase 2: Camera Sensor Init ---");
    ret = board_laiwfs300_camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera sensor init FAILED: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "face detection is blocked until SP0A39 responds and DVP PCLK/VSYNC become active");
        ESP_LOGE(TAG, "Screen shows RED = camera hardware or camera power/clock failure");
        display_hal_fill_rgb565(0xF800);
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(CAMERA_DIAG_INTERVAL_MS));
            ESP_LOGI(TAG, "[camera_diag] re-checking DVP signals...");
            int pclk = dvp_check_signal(to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_PCLK), "PCLK");
            int vsync = dvp_check_signal(to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_VSYNC), "VSYNC");
            int hsync = dvp_check_signal(to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_HSYNC), "HSYNC");
            ESP_LOGW(TAG,
                     "[camera_diag] camera_init=fail err=%s sccb=0x%02x pclk_gpio=%d vsync_gpio=%d hsync_gpio=%d pclk_changes=%d vsync_changes=%d hsync_changes=%d",
                     esp_err_to_name(ret),
                     BOARD_LAIWFS300_SP0A39_I2C_ADDR_7BIT,
                     BOARD_LAIWFS300_GPIO_CAMERA_PCLK,
                     BOARD_LAIWFS300_GPIO_CAMERA_VSYNC,
                     BOARD_LAIWFS300_GPIO_CAMERA_HSYNC,
                     pclk,
                     vsync,
                     hsync);
            if (0 == pclk && 0 == vsync && 0 == hsync) {
                ESP_LOGW(TAG,
                         "[camera_diag] root_hint=sensor_inactive check=C0_20MHz_MCLK,E0_camera_board,FPC_orientation,IOEX_RESET_PWDN");
            }
            if (pclk > 0 && vsync > 0) {
                ESP_LOGI(TAG, "[camera_diag] DVP signals active! Restart device to proceed.");
            }
        }
    }
    ESP_LOGI(TAG, "  sensor initialized and registers written");
    ESP_ERROR_CHECK(camera_hal_log_sensor_output_regs());

    ESP_LOGI(TAG, "--- Phase 3: Face Detector Init ---");
    if (FACE_DETECTION_ENABLED) {
        ret = init_face_detector();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "face detector init FAILED: %s", esp_err_to_name(ret));
            display_hal_fill_rgb565(0xFFE0);
            return;
        }
        ret = start_face_detect_task();
        if (ret != ESP_OK) {
            display_hal_fill_rgb565(0xFFE0);
            return;
        }
    } else {
        ESP_LOGW(TAG, "[diagnostic] face detection disabled for display isolation");
    }

    ESP_LOGI(TAG, "--- Phase 4: DVP Controller Init ---");
    size_t fb_size = CAM_H_RES * CAM_V_RES * 2;
    for (int i = 0; i < FB_COUNT; i++) {
        s_fb[i] = (uint8_t *)heap_caps_aligned_alloc(64, fb_size, MALLOC_CAP_SPIRAM);
        if (s_fb[i] == NULL) {
            ESP_LOGE(TAG, "framebuffer %d alloc failed (%u bytes)", i, (unsigned)fb_size);
            return;
        }
        memset(s_fb[i], 0, fb_size);
    }
    ESP_LOGI(TAG, "  %d framebuffers allocated (%u KB each)", FB_COUNT, (unsigned)(fb_size / 1024));

    if (FROZEN_CAMERA_FRAME_DIAG_ENABLED) {
        s_frozen_camera_frame = (uint8_t *)heap_caps_aligned_alloc(64, fb_size, MALLOC_CAP_SPIRAM);
        if (NULL == s_frozen_camera_frame) {
            ESP_LOGE(TAG, "frozen camera frame alloc failed (%u bytes)", (unsigned)fb_size);
            return;
        }
        memset(s_frozen_camera_frame, 0, fb_size);
        ESP_LOGI(TAG, "  frozen camera frame buffer allocated (%u KB)", (unsigned)(fb_size / 1024));
    }

    if (DETACHED_RGB_FRAME_DIAG_ENABLED) {
        const size_t rgb_frame_size = PREVIEW_H_RES * PREVIEW_V_RES * sizeof(uint16_t);
        s_detached_rgb_frame = (uint16_t *)heap_caps_aligned_alloc(64,
                                                                  rgb_frame_size,
                                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (NULL == s_detached_rgb_frame) {
            ESP_LOGE(TAG, "detached RGB frame alloc failed (%u bytes)", (unsigned)rgb_frame_size);
            return;
        }
        memset(s_detached_rgb_frame, 0, rgb_frame_size);
        ESP_LOGI(TAG, "  detached RGB frame buffer allocated (%u KB, internal DMA)",
                 (unsigned)(rgb_frame_size / 1024));
    }

    size_t chunk_buf_size = PREVIEW_H_RES * LCD_CHUNK_LINES * sizeof(uint16_t);
    for (int i = 0; i < LCD_CHUNK_BUF_CAPACITY; ++i) {
        s_rgb_chunk_buf[i] = (uint16_t *)heap_caps_aligned_alloc(64, chunk_buf_size, MALLOC_CAP_DMA);
        if (s_rgb_chunk_buf[i] == NULL) {
            if (0 == i) {
                ESP_LOGE(TAG, "RGB chunk buffer %d alloc failed (%u bytes)", i, (unsigned)chunk_buf_size);
                return;
            }
            ESP_LOGW(TAG,
                     "RGB chunk buffer %d alloc failed (%u bytes); fallback to synchronous single buffer",
                     i,
                     (unsigned)chunk_buf_size);
            break;
        }
        s_rgb_chunk_buf_count++;
    }
    ESP_LOGI(TAG,
             "  lcd_chunk_buffers=%d/%d x %u bytes transfer_mode=%s internal_free=%u internal_largest=%u",
             s_rgb_chunk_buf_count,
             LCD_CHUNK_BUF_CAPACITY,
             (unsigned)chunk_buf_size,
             (s_rgb_chunk_buf_count > 1) ? "pipelined" : "synchronous",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  display_path=camera_display_demo_stable detection_input=read_only_framebuffer_after_dvp_stop");
    ESP_LOGI(TAG,
             "  diagnostic_display_source=%s live_camera_framebuffer_reads=%s",
             FIXED_COLOR_BARS_DIAG_ENABLED ? "fixed_color_bars" :
                 (FROZEN_CAMERA_FRAME_DIAG_ENABLED ? "frozen_camera_vyuy" :
                     (DETACHED_RGB_FRAME_DIAG_ENABLED ? "detached_live_rgb565" : "live_camera_vyuy")),
             (FIXED_COLOR_BARS_DIAG_ENABLED || FROZEN_CAMERA_FRAME_DIAG_ENABLED) ?
                 "one_time_only" :
                 (DETACHED_RGB_FRAME_DIAG_ENABLED ? "convert_then_release_before_lcd" : "enabled"));
    ESP_LOGI(TAG,
             "  rgb_detach_capture_gate=%s",
             PAUSE_CAPTURE_DURING_RGB_DETACH_DIAG_ENABLED ?
                 "stop_disable_until_lcd_complete" : "capture_running_during_conversion");
    init_yuv_to_rgb_lut();

    s_frame_ready = xSemaphoreCreateBinary();
    if (s_frame_ready == NULL) {
        ESP_LOGE(TAG, "frame ready semaphore alloc failed");
        return;
    }

    esp_cam_ctlr_dvp_pin_config_t dvp_pins = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io = {
            [0] = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_D0),
            [1] = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_D1),
            [2] = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_D2),
            [3] = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_D3),
            [4] = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_D4),
            [5] = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_D5),
            [6] = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_D6),
            [7] = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_D7),
            [8] = GPIO_NUM_NC,
            [9] = GPIO_NUM_NC,
            [10] = GPIO_NUM_NC,
            [11] = GPIO_NUM_NC,
            [12] = GPIO_NUM_NC,
            [13] = GPIO_NUM_NC,
            [14] = GPIO_NUM_NC,
            [15] = GPIO_NUM_NC,
        },
        .vsync_io = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_VSYNC),
        .de_io = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_HSYNC),
        .pclk_io = to_gpio_num(BOARD_LAIWFS300_GPIO_CAMERA_PCLK),
        .xclk_io = GPIO_NUM_NC,
    };

    esp_cam_ctlr_dvp_config_t dvp_cfg = {
        .ctlr_id = 0,
        .clk_src = CAM_CLK_SRC_DEFAULT,
        .h_res = CAM_H_RES,
        .v_res = CAM_V_RES,
        .input_data_color_type = CAM_CTLR_COLOR_YUV422,
        .cam_data_width = 8,
        .bit_swap_en = 0,
        .byte_swap_en = 0,
        .bk_buffer_dis = 0,
        .pin_dont_init = 0,
        .pic_format_jpeg = 0,
        .external_xtal = 1,
        .dma_burst_size = DVP_DMA_BURST_SIZE,
        .xclk_freq = 0,
        .pin = &dvp_pins,
    };

    ret = esp_cam_new_dvp_ctlr(&dvp_cfg, &s_cam_ctlr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DVP controller create failed: %s", esp_err_to_name(ret));
        display_hal_fill_rgb565(0xFFE0);
        return;
    }
    ESP_LOGI(TAG, "  DVP controller created, dma_burst=%u", (unsigned)DVP_DMA_BURST_SIZE);

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = on_get_new_trans,
        .on_trans_finished = on_trans_finished,
    };
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(s_cam_ctlr, &cbs, NULL));
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(s_cam_ctlr));

    s_active_fb = 0;
    ESP_ERROR_CHECK(esp_cam_ctlr_start(s_cam_ctlr));
    ESP_LOGI(TAG, "  DVP capture started");
    ESP_LOGI(TAG, "  detection_capture_policy=stop_disable_then_enable_start");
    ESP_LOGI(TAG, "  frame_handoff=completed_transaction framebuffer_access=read_only_locked");
    ESP_LOGI(TAG, "  face_box_overlay=%s",
             FACE_BOX_OVERLAY_ENABLED ? "post_frame_rectangles" : "disabled_for_diagnostic");
    ESP_LOGI(TAG,
             "  capture_gate_overlap=lcd_core%d+detect_core%d dvp_resume=%s locked_fb_release=%s",
             xPortGetCoreID(),
             FACE_DETECT_TASK_CORE,
             PAUSE_CAPTURE_DURING_RGB_DETACH_DIAG_ENABLED ? "after_lcd_complete" : "after_detect",
             DETACHED_RGB_FRAME_DIAG_ENABLED ? "after_rgb_detach" : "after_lcd");

    ESP_LOGI(TAG, "--- Phase 5: Live Preview + Face Detection ---");
    uint32_t last_stats_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t last_capture_count = s_frame_count;
    uint32_t last_display_count = s_display_frame_count;
    uint32_t last_detect_count = s_detect_frame_count;
    uint32_t last_selected_source_sequence = 0;
    uint32_t window_fresh_display_frames = 0;
    uint32_t window_stale_display_frames = 0;
    uint64_t window_convert_us = 0;
    uint64_t window_draw_us = 0;
    uint64_t window_render_us = 0;
    uint64_t window_render_max_us = 0;
    int chunk_buf_idx = 0;
    ESP_LOGI(TAG, "[preview] fixed pixel format %s", pixfmt_name(s_pixfmt));

    while (1) {
        if (pdTRUE != xSemaphoreTake(s_frame_ready, pdMS_TO_TICKS(2000))) {
            s_error_count = s_error_count + 1U;
            ESP_LOGW(TAG,
                     "[preview] frame timeout #%lu (total_frames=%lu)",
                     (unsigned long)s_error_count,
                     (unsigned long)s_frame_count);
            if (s_error_count > 10 && s_frame_count == 0) {
                ESP_LOGE(TAG, "no frames received after 20s - hardware issue");
                display_hal_fill_rgb565(0xF81F);
                break;
            }
            continue;
        }

        int fb_idx = -1;
        uint32_t frame_sequence = 0;
        bool framebuffer_locked = false;
        portENTER_CRITICAL(&s_framebuffer_lock);
        fb_idx = s_latest_completed_fb;
        if (0 <= fb_idx && fb_idx < FB_COUNT) {
            if (!FIXED_COLOR_BARS_DIAG_ENABLED && !s_frozen_camera_frame_ready) {
                s_locked_fb = fb_idx;
                framebuffer_locked = true;
            }
            frame_sequence = s_fb_completed_sequence[fb_idx];
        }
        portEXIT_CRITICAL(&s_framebuffer_lock);
        if (fb_idx < 0 || fb_idx >= FB_COUNT) {
            s_error_count = s_error_count + 1U;
            ESP_LOGE(TAG, "[preview] no completed framebuffer after ready signal");
            continue;
        }

        face_detect_request_t rgb_detach_gate = {};
        bool rgb_detach_capture_paused = false;
        if (PAUSE_CAPTURE_DURING_RGB_DETACH_DIAG_ENABLED) {
            esp_err_t pause_ret = pause_capture_for_detection(&rgb_detach_gate.capture_pause_cycle,
                                                              &rgb_detach_gate.capture_pause_started_us);
            if (ESP_OK != pause_ret) {
                s_error_count = s_error_count + 1U;
                ESP_LOGE(TAG, "[rgb_detach] capture pause failed: %s", esp_err_to_name(pause_ret));
                portENTER_CRITICAL(&s_framebuffer_lock);
                s_locked_fb = -1;
                portEXIT_CRITICAL(&s_framebuffer_lock);
                continue;
            }
            rgb_detach_capture_paused = true;
        }

        if (frame_sequence != 0U && frame_sequence != last_selected_source_sequence) {
            window_fresh_display_frames++;
        } else {
            window_stale_display_frames++;
        }
        last_selected_source_sequence = frame_sequence;
        s_last_display_sequence = frame_sequence;
        if (!FIXED_COLOR_BARS_DIAG_ENABLED && !s_frozen_camera_frame_ready) {
            esp_cache_msync(s_fb[fb_idx], CAM_H_RES * CAM_V_RES * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        }

        if (FROZEN_CAMERA_FRAME_DIAG_ENABLED && !s_frozen_camera_frame_ready) {
            const uint64_t copy_started_us = (uint64_t)esp_timer_get_time();
            memcpy(s_frozen_camera_frame, s_fb[fb_idx], CAM_H_RES * CAM_V_RES * 2);
            s_frozen_camera_frame_sequence = frame_sequence;
            s_frozen_camera_frame_ready = true;
            portENTER_CRITICAL(&s_framebuffer_lock);
            s_locked_fb = -1;
            portEXIT_CRITICAL(&s_framebuffer_lock);
            framebuffer_locked = false;
            ESP_LOGI(TAG,
                     "[frozen_frame] ready source_seq=%lu copy_ms=%llu bytes=%u",
                     (unsigned long)s_frozen_camera_frame_sequence,
                     (unsigned long long)(((uint64_t)esp_timer_get_time() - copy_started_us) / 1000U),
                     (unsigned)(CAM_H_RES * CAM_V_RES * 2));
        }

        uint64_t frame_convert_us = 0;
        uint64_t frame_draw_us = 0;
        uint64_t frame_render_start_us = (uint64_t)esp_timer_get_time();
        if (DETACHED_RGB_FRAME_DIAG_ENABLED) {
            const uint64_t convert_start_us = (uint64_t)esp_timer_get_time();
            vyuy_subsample_to_rgb565_chunk(s_fb[fb_idx],
                                           s_detached_rgb_frame,
                                           0,
                                           PREVIEW_V_RES);
            frame_convert_us += (uint64_t)esp_timer_get_time() - convert_start_us;

            portENTER_CRITICAL(&s_framebuffer_lock);
            s_locked_fb = -1;
            portEXIT_CRITICAL(&s_framebuffer_lock);
            framebuffer_locked = false;
        }

        update_display_face_result();
        const bool detect_due = FACE_DETECTION_ENABLED &&
                                ((s_display_frame_count % FACE_DETECT_INTERVAL_FRAMES) == 0U);
        face_detect_request_t detect_request = {};
        const bool prepare_detection = detect_due &&
                                       begin_face_detection(s_display_frame_count,
                                                            frame_sequence,
                                                            &detect_request);

        if (prepare_detection) {
            esp_err_t pause_ret = pause_capture_for_detection(&detect_request.capture_pause_cycle,
                                                              &detect_request.capture_pause_started_us);
            if (ESP_OK == pause_ret) {
                detect_request.framebuffer = s_fb[fb_idx];
                if (!submit_face_detection(&detect_request)) {
                    esp_err_t resume_ret = resume_capture_after_detection(&detect_request);
                    if (ESP_OK != resume_ret) {
                        ESP_LOGE(TAG, "[capture_gate] queue failure recovery failed: %s",
                                 esp_err_to_name(resume_ret));
                    }
                }
            } else {
                xSemaphoreGive(s_face_rgb_available);
            }
        }

        while (ESP_OK == display_hal_wait_pending(0)) {
        }

        lcd_pending_draw_t pending_draw = {};
        for (int y = 0; y < PREVIEW_V_RES; y += LCD_CHUNK_LINES) {
            int lines = LCD_CHUNK_LINES;
            if (y + lines > PREVIEW_V_RES) {
                lines = PREVIEW_V_RES - y;
            }

            if (pending_draw.active && (pending_draw.buffer_idx == chunk_buf_idx)) {
                ESP_ERROR_CHECK(wait_lcd_draw_completion(&pending_draw, &frame_draw_us));
            }

            uint64_t convert_start_us = esp_timer_get_time();
            if (FIXED_COLOR_BARS_DIAG_ENABLED) {
                fill_fixed_color_bars_chunk(s_rgb_chunk_buf[chunk_buf_idx], (uint32_t)lines);
            } else if (DETACHED_RGB_FRAME_DIAG_ENABLED) {
                memcpy(s_rgb_chunk_buf[chunk_buf_idx],
                       s_detached_rgb_frame + ((size_t)y * PREVIEW_H_RES),
                       (size_t)PREVIEW_H_RES * (size_t)lines * sizeof(uint16_t));
            } else {
                const uint8_t *display_framebuffer = FROZEN_CAMERA_FRAME_DIAG_ENABLED ?
                                                         s_frozen_camera_frame : s_fb[fb_idx];
                vyuy_subsample_to_rgb565_chunk(display_framebuffer, s_rgb_chunk_buf[chunk_buf_idx],
                                               (uint32_t)y, (uint32_t)lines);
            }
            frame_convert_us += esp_timer_get_time() - convert_start_us;

            if (pending_draw.active) {
                ESP_ERROR_CHECK(wait_lcd_draw_completion(&pending_draw, &frame_draw_us));
            }

            uint64_t draw_start_us = (uint64_t)esp_timer_get_time();
            ESP_ERROR_CHECK(display_hal_draw_bitmap_rgb565(PREVIEW_X_OFFSET, PREVIEW_Y_OFFSET + y,
                                                           PREVIEW_H_RES, lines,
                                                           s_rgb_chunk_buf[chunk_buf_idx]));
            pending_draw.active = true;
            pending_draw.buffer_idx = chunk_buf_idx;
            pending_draw.started_us = draw_start_us;

            chunk_buf_idx = (chunk_buf_idx + 1) % s_rgb_chunk_buf_count;
        }
        ESP_ERROR_CHECK(wait_lcd_draw_completion(&pending_draw, &frame_draw_us));
        if (FACE_BOX_OVERLAY_ENABLED) {
            ESP_ERROR_CHECK(draw_face_boxes_overlay(&frame_draw_us));
        }
        if (rgb_detach_capture_paused) {
            esp_err_t resume_ret = resume_capture_after_detection(&rgb_detach_gate);
            if (ESP_OK != resume_ret) {
                s_error_count = s_error_count + 1U;
                ESP_LOGE(TAG, "[rgb_detach] capture resume failed: %s", esp_err_to_name(resume_ret));
                break;
            }
            rgb_detach_capture_paused = false;
        }
        uint64_t frame_render_us = (uint64_t)esp_timer_get_time() - frame_render_start_us;

        window_convert_us += frame_convert_us;
        window_draw_us += frame_draw_us;
        window_render_us += frame_render_us;
        window_render_max_us = std::max(window_render_max_us, frame_render_us);
        s_display_frame_count++;

        if (framebuffer_locked) {
            portENTER_CRITICAL(&s_framebuffer_lock);
            s_locked_fb = -1;
            portEXIT_CRITICAL(&s_framebuffer_lock);
        }

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms - last_stats_time >= PREVIEW_STATS_INTERVAL_MS) {
            uint32_t elapsed_ms = now_ms - last_stats_time;
            uint32_t capture_frames = s_frame_count - last_capture_count;
            uint32_t display_frames = s_display_frame_count - last_display_count;
            uint32_t total_detect_frames = 0;
            uint32_t total_detect_requests = 0;
            uint32_t detect_request_drops = 0;
            uint32_t detect_errors = 0;
            uint32_t capture_pause_count = 0;
            uint32_t capture_pause_errors = 0;
            uint64_t detect_window_us = 0;
            uint64_t detect_last_us = 0;
            uint64_t capture_pause_window_us = 0;
            uint64_t detect_prepare_window_us = 0;
            portENTER_CRITICAL(&s_detect_metrics_lock);
            total_detect_frames = s_detect_frame_count;
            total_detect_requests = s_detect_request_count;
            detect_request_drops = s_detect_request_drop_count;
            detect_errors = s_detect_error_count;
            capture_pause_count = s_capture_pause_count;
            capture_pause_errors = s_capture_pause_error_count;
            detect_window_us = s_window_detect_us;
            detect_last_us = s_last_detect_us;
            capture_pause_window_us = s_capture_pause_us;
            detect_prepare_window_us = s_detect_prepare_us;
            s_window_detect_us = 0;
            s_capture_pause_us = 0;
            s_detect_prepare_us = 0;
            portEXIT_CRITICAL(&s_detect_metrics_lock);

            uint32_t detect_frames = total_detect_frames - last_detect_count;
            uint32_t dropped_frames = (capture_frames > display_frames) ? (capture_frames - display_frames) : 0;
            float capture_fps = (float)capture_frames * 1000.0f / (float)elapsed_ms;
            float display_fps = (float)display_frames * 1000.0f / (float)elapsed_ms;
            float fresh_fps = (float)window_fresh_display_frames * 1000.0f / (float)elapsed_ms;
            float detect_fps = (float)detect_frames * 1000.0f / (float)elapsed_ms;
            uint64_t avg_convert_ms = (display_frames > 0) ? (window_convert_us / display_frames) / 1000 : 0;
            uint64_t avg_draw_ms = (display_frames > 0) ? (window_draw_us / display_frames) / 1000 : 0;
            uint64_t avg_render_ms = (display_frames > 0) ? (window_render_us / display_frames) / 1000 : 0;
            uint64_t avg_detect_ms = (detect_frames > 0) ? (detect_window_us / detect_frames) / 1000 : 0;
            uint64_t avg_capture_pause_ms = (detect_frames > 0) ?
                                                   (capture_pause_window_us / detect_frames) / 1000 : 0;
            uint64_t avg_detect_prepare_ms = (detect_frames > 0) ?
                                                 (detect_prepare_window_us / detect_frames) / 1000 : 0;
            if (FACE_DETECTION_ENABLED) {
                ESP_LOGI(TAG,
                         "[preview] cap=%.1f disp=%.1f fresh=%.1f detect=%.1f faces=%d seq=%lu/%lu stale=%lu dropped=%lu errors=%lu detect_errors=%lu requests=%lu request_drops=%lu pauses=%lu pause_errors=%lu pause_avg=%llums prep=%llums convert=%llums draw=%llums render=%llums render_max=%llums detect_avg=%llums detect_last=%llums",
                         capture_fps,
                         display_fps,
                         fresh_fps,
                         detect_fps,
                         s_display_face_count,
                         (unsigned long)s_last_display_sequence,
                         (unsigned long)s_frame_count,
                         (unsigned long)window_stale_display_frames,
                         (unsigned long)dropped_frames,
                         (unsigned long)s_error_count,
                         (unsigned long)detect_errors,
                         (unsigned long)total_detect_requests,
                         (unsigned long)detect_request_drops,
                         (unsigned long)capture_pause_count,
                         (unsigned long)capture_pause_errors,
                         (unsigned long long)avg_capture_pause_ms,
                         (unsigned long long)avg_detect_prepare_ms,
                         (unsigned long long)avg_convert_ms,
                         (unsigned long long)avg_draw_ms,
                         (unsigned long long)avg_render_ms,
                         (unsigned long long)(window_render_max_us / 1000U),
                         (unsigned long long)avg_detect_ms,
                         (unsigned long long)(detect_last_us / 1000U));
            } else {
                ESP_LOGI(TAG,
                         "[preview] cap=%.1f disp=%.1f fresh=%.1f seq=%lu/%lu stale=%lu dropped=%lu errors=%lu convert=%llums draw=%llums render=%llums render_max=%llums",
                         capture_fps,
                         display_fps,
                         fresh_fps,
                         (unsigned long)s_last_display_sequence,
                         (unsigned long)s_frame_count,
                         (unsigned long)window_stale_display_frames,
                         (unsigned long)dropped_frames,
                         (unsigned long)s_error_count,
                         (unsigned long long)avg_convert_ms,
                         (unsigned long long)avg_draw_ms,
                         (unsigned long long)avg_render_ms,
                         (unsigned long long)(window_render_max_us / 1000U));
            }

            last_stats_time = now_ms;
            last_capture_count = s_frame_count;
            last_display_count = s_display_frame_count;
            last_detect_count = total_detect_frames;
            window_fresh_display_frames = 0;
            window_stale_display_frames = 0;
            window_convert_us = 0;
            window_draw_us = 0;
            window_render_us = 0;
            window_render_max_us = 0;
        }
    }

    esp_cam_ctlr_stop(s_cam_ctlr);
    esp_cam_ctlr_disable(s_cam_ctlr);
    ESP_LOGI(TAG, "demo stopped");
}
