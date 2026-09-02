#include "companion_motion.h"
#include "companion_turn_control.h"

#include "board_laiwfs300.h"
#include "bmi260_imu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stddef.h>

#define COMPANION_MOTION_QUEUE_DEPTH 4U
#define COMPANION_MOTION_TASK_STACK 4096U
#define COMPANION_MOTION_TASK_PRIORITY 4U
#define COMPANION_MOTION_NOTIFY_STOP (1UL << 0)
#define COMPANION_MOTION_COMMAND_LOCK_MS 20U
#define COMPANION_IMU_SETTLE_WINDOW_SAMPLES 20U
#define COMPANION_IMU_SETTLE_REQUIRED_WINDOWS 2U
#define COMPANION_IMU_SETTLE_MAX_WINDOWS 4U
#define COMPANION_IMU_SETTLE_GYRO_RANGE_RAW 82
#define COMPANION_IMU_SETTLE_ACCEL_RANGE_RAW 800.0f
#define COMPANION_IMU_CALIBRATION_SAMPLES 50U
#define COMPANION_IMU_CALIBRATION_TRIM_SAMPLES 10U
#define COMPANION_IMU_CALIBRATION_MAX_RANGE_RAW 82
#define COMPANION_IMU_CALIBRATION_ATTEMPTS 2U
#define COMPANION_IMU_SAMPLE_MS 10U
#define COMPANION_IMU_PROGRESS_LOG_SAMPLES 20U
#define COMPANION_MOTION_ROLE_COUNT 2U
#define COMPANION_MERIT_SAMPLE_PERIOD_MS 10U
#define COMPANION_MERIT_ERROR_LOG_INTERVAL_US 5000000ULL
#define COMPANION_MERIT_DIAG_REST_INTERVAL_SAMPLES 50U
#define COMPANION_MERIT_DIAG_ACCEL_ACTIVITY_RAW 500U
#define COMPANION_MERIT_DIAG_GYRO_ACTIVITY_RAW 200U
#define COMPANION_MERIT_DIAG_ACTIVITY_INTERVAL_SAMPLES 10U
#define COMPANION_MERIT_MOTION_START_SUPPRESS_US 150000ULL
#define COMPANION_MERIT_MOTION_STOP_SUPPRESS_US 250000ULL

typedef struct {
    companion_motion_command_t command;
    uint64_t role_epoch;
} motion_queue_item_t;

static const char *TAG = "companion_motion";

static companion_motion_config_t s_config;
static robot_motion_t *s_motion;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_command_lock;
static TaskHandle_t s_task;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_started;
static bool s_starting;
static bool s_available;
static uint64_t s_role_epochs[COMPANION_MOTION_ROLE_COUNT];
static companion_motion_role_t s_active_role;
static uint64_t s_active_role_epoch;
static bool s_active_role_valid;
static companion_merit_tap_t s_merit_detector;
static bool s_merit_gate_enabled;
static uint32_t s_merit_gate_generation;
static uint32_t s_merit_gate_wake_seq;
static uint32_t s_merit_gate_revision;
static uint32_t s_merit_detector_revision;
static bool s_merit_imu_initialized;
static uint64_t s_merit_last_error_log_us;
static uint64_t s_merit_suppress_until_us;
static uint32_t s_merit_diag_samples;

static uint32_t merit_vector_magnitude_raw(int16_t x, int16_t y, int16_t z)
{
    const int64_t x_value = x;
    const int64_t y_value = y;
    const int64_t z_value = z;
    const uint64_t square_sum = (uint64_t)(x_value * x_value) +
                                (uint64_t)(y_value * y_value) +
                                (uint64_t)(z_value * z_value);
    return (uint32_t)sqrt((double)square_sum);
}

static bool role_is_valid(companion_motion_role_t role)
{
    return COMPANION_MOTION_ROLE_ROAM == role ||
           COMPANION_MOTION_ROLE_WAKE_TURN == role;
}

static uint64_t current_role_epoch(companion_motion_role_t role)
{
    uint64_t epoch = 0ULL;
    portENTER_CRITICAL(&s_state_lock);
    if (role_is_valid(role)) {
        epoch = s_role_epochs[role];
    }
    portEXIT_CRITICAL(&s_state_lock);
    return epoch;
}

static bool motion_cancelled(companion_motion_role_t role,
                             uint64_t command_epoch)
{
    return !role_is_valid(role) ||
           command_epoch != current_role_epoch(role);
}

static void set_available(bool available)
{
    portENTER_CRITICAL(&s_state_lock);
    s_available = available;
    portEXIT_CRITICAL(&s_state_lock);
}

static void suppress_merit_tap(uint64_t duration_us, const char *reason)
{
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    s_merit_suppress_until_us = now_us + duration_us;
    companion_merit_tap_reset(&s_merit_detector);
    s_merit_diag_samples = 0U;
    ESP_LOGI(TAG, "[DEBUG-MERIT-SUPPRESS] reason=%s duration_us=%llu",
             NULL != reason ? reason : "unknown",
             (unsigned long long)duration_us);
}

static void poll_merit_tap(void)
{
    bool enabled = false;
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    uint32_t revision = 0U;
    portENTER_CRITICAL(&s_state_lock);
    enabled = s_merit_gate_enabled;
    generation = s_merit_gate_generation;
    wake_seq = s_merit_gate_wake_seq;
    revision = s_merit_gate_revision;
    portEXIT_CRITICAL(&s_state_lock);
    if (!enabled) {
        return;
    }
    if (revision != s_merit_detector_revision) {
        companion_merit_tap_reset(&s_merit_detector);
        s_merit_detector_revision = revision;
        s_merit_diag_samples = 0U;
        ESP_LOGI(TAG,
                 "[DEBUG-MERIT-POLL] detector reset revision=%lu generation=%lu wake_seq=%lu",
                 (unsigned long)revision, (unsigned long)generation,
                 (unsigned long)wake_seq);
    }
    if ((uint64_t)esp_timer_get_time() < s_merit_suppress_until_us) {
        return;
    }
    if (!s_merit_imu_initialized) {
        const esp_err_t init_result = bmi260_init();
        if (ESP_OK != init_result) {
            const uint64_t now_us = (uint64_t)esp_timer_get_time();
            if (now_us - s_merit_last_error_log_us >=
                COMPANION_MERIT_ERROR_LOG_INTERVAL_US) {
                s_merit_last_error_log_us = now_us;
                ESP_LOGW(TAG, "merit IMU init failed: %s",
                         esp_err_to_name(init_result));
            }
            return;
        }
        s_merit_imu_initialized = true;
        ESP_LOGI(TAG,
                 "[DEBUG-MERIT-IMU] ready acc_range=0x01 gyro_range=0x00 sample_period_ms=%u",
                 COMPANION_MERIT_SAMPLE_PERIOD_MS);
    }
    bmi260_raw_data_t accel = {0};
    bmi260_raw_data_t gyro = {0};
    esp_err_t result = bmi260_read_accel(&accel);
    if (ESP_OK == result) {
        result = bmi260_read_gyro(&gyro);
    }
    if (ESP_OK != result) {
        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        if (now_us - s_merit_last_error_log_us >=
            COMPANION_MERIT_ERROR_LOG_INTERVAL_US) {
            s_merit_last_error_log_us = now_us;
            ESP_LOGW(TAG, "merit IMU sample failed: %s",
                     esp_err_to_name(result));
        }
        return;
    }
    const companion_merit_sample_t sample = {
        .accel_x = accel.x,
        .accel_y = accel.y,
        .accel_z = accel.z,
        .gyro_x = gyro.x,
        .gyro_y = gyro.y,
        .gyro_z = gyro.z,
        .timestamp_us = (uint64_t)esp_timer_get_time(),
    };
    const bool initialized_before = s_merit_detector.initialized;
    const bool candidate_before = s_merit_detector.candidate_active;
    const uint64_t candidate_deadline_before =
        s_merit_detector.candidate_deadline_us;
    const uint32_t accel_raw = merit_vector_magnitude_raw(
        sample.accel_x, sample.accel_y, sample.accel_z);
    const uint32_t gyro_raw = merit_vector_magnitude_raw(
        sample.gyro_x, sample.gyro_y, sample.gyro_z);
    const uint32_t baseline_before =
        (uint32_t)s_merit_detector.baseline_accel_raw;
    const uint32_t previous_before =
        (uint32_t)s_merit_detector.previous_accel_raw;
    const uint32_t sample_delta = (accel_raw >= previous_before) ?
        (accel_raw - previous_before) : (previous_before - accel_raw);
    const int32_t baseline_delta =
        (int32_t)accel_raw - (int32_t)baseline_before;
    const bool peak_allowed = initialized_before;
    const uint32_t baseline_delta_abs = (0 <= baseline_delta) ?
        (uint32_t)baseline_delta : (uint32_t)(-baseline_delta);
    const bool delta_allowed =
        sample_delta >= s_merit_detector.config.accel_delta_threshold_raw ||
        baseline_delta_abs >=
            s_merit_detector.config.accel_delta_threshold_raw;
    const bool acceleration_candidate = initialized_before && delta_allowed;
    const bool gyro_allowed = true;
    const bool candidate_timed_out = initialized_before && candidate_before &&
        sample.timestamp_us > candidate_deadline_before;
    companion_merit_result_t merit_result = {0};
    result = companion_merit_tap_push(&s_merit_detector, &sample,
                                      &merit_result);
    s_merit_diag_samples++;
    const bool candidate_after = s_merit_detector.candidate_active;
    const bool accel_activity =
        sample_delta >= COMPANION_MERIT_DIAG_ACCEL_ACTIVITY_RAW ||
        baseline_delta >= (int32_t)COMPANION_MERIT_DIAG_ACCEL_ACTIVITY_RAW ||
        baseline_delta <= -(int32_t)COMPANION_MERIT_DIAG_ACCEL_ACTIVITY_RAW;
    const bool candidate_started = !candidate_before && candidate_after;
    const bool candidate_waiting = candidate_before && candidate_after;
    const bool diagnostic_sample = !initialized_before || merit_result.hit ||
        candidate_started || candidate_timed_out ||
        (accel_activity && 0U == (s_merit_diag_samples %
            COMPANION_MERIT_DIAG_ACTIVITY_INTERVAL_SAMPLES)) ||
        0U == (s_merit_diag_samples %
               COMPANION_MERIT_DIAG_REST_INTERVAL_SAMPLES);
    if (diagnostic_sample) {
        const char *reason = !initialized_before ? "baseline" :
            candidate_timed_out ? "confirm_timeout" :
            merit_result.hit ? "hit" :
            candidate_started ? "candidate" :
            candidate_waiting ? "confirm_wait" :
            acceleration_candidate ? "activity" :
            !peak_allowed ? "baseline" : "below_delta";
        ESP_LOGI(TAG,
                 "[DEBUG-MERIT-RAW] seq=%lu reason=%s ax=%d ay=%d az=%d gx=%d gy=%d gz=%d amag=%lu gmag=%lu base=%lu base_delta=%ld prev=%lu sample_delta=%lu range=%lu excursion=%lu dyn_threshold=%lu return=%lu peak_ok=%u delta_ok=%u gyro_ok=%u candidate=%u->%u result=%s",
                 (unsigned long)merit_result.sample_seq, reason,
                 sample.accel_x, sample.accel_y, sample.accel_z,
                 sample.gyro_x, sample.gyro_y, sample.gyro_z,
                 (unsigned long)accel_raw, (unsigned long)gyro_raw,
                 (unsigned long)baseline_before, (long)baseline_delta,
                 (unsigned long)previous_before, (unsigned long)sample_delta,
                 (unsigned long)s_merit_detector.last_baseline_range_raw,
                 (unsigned long)s_merit_detector.last_excursion_raw,
                 (unsigned long)s_merit_detector.last_dynamic_threshold_raw,
                 (unsigned long)s_merit_detector.config.return_threshold_raw,
                 peak_allowed ? 1U : 0U, delta_allowed ? 1U : 0U,
                 gyro_allowed ? 1U : 0U, candidate_before ? 1U : 0U,
                 candidate_after ? 1U : 0U,
                 esp_err_to_name(result));
    }
    if (ESP_OK == result && merit_result.hit &&
        NULL != s_config.on_merit_tap) {
        ESP_LOGI(TAG,
                 "[DEBUG-MERIT-HIT] callback sample=%lu accel=%ld gyro=%ld generation=%lu wake_seq=%lu repeat=%lu",
                 (unsigned long)merit_result.sample_seq,
                 (long)merit_result.accel_peak_raw,
                 (long)merit_result.gyro_peak_raw,
                 (unsigned long)generation, (unsigned long)wake_seq,
                 (unsigned long)merit_result.repeat_count);
        s_config.on_merit_tap(&merit_result, generation, wake_seq,
                              sample.timestamp_us, s_config.user_ctx);
    }
}

static esp_err_t wait_roam_duration(const companion_motion_command_t *command,
                                    uint64_t command_epoch,
                                    uint32_t *notify_value)
{
    if (NULL == command || NULL == notify_value) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t remaining_ms = command->duration_ms;
    while (0U < remaining_ms) {
        const uint32_t slice_ms =
            (remaining_ms > COMPANION_MERIT_SAMPLE_PERIOD_MS) ?
            COMPANION_MERIT_SAMPLE_PERIOD_MS : remaining_ms;
        (void)xTaskNotifyWait(0U, UINT32_MAX, notify_value,
                              pdMS_TO_TICKS(slice_ms));
        if (motion_cancelled(command->role, command_epoch)) {
            return ESP_ERR_INVALID_STATE;
        }
        poll_merit_tap();
        remaining_ms -= slice_ms;
    }
    return ESP_OK;
}

static const char *action_name(companion_action_t action)
{
    switch (action) {
    case COMPANION_ACTION_STOP: return "stop";
    case COMPANION_ACTION_FORWARD: return "forward";
    case COMPANION_ACTION_BACKWARD: return "backward";
    case COMPANION_ACTION_TURN_LEFT: return "turn_left";
    case COMPANION_ACTION_TURN_RIGHT: return "turn_right";
    case COMPANION_ACTION_UTURN_LEFT: return "uturn_left";
    case COMPANION_ACTION_UTURN_RIGHT: return "uturn_right";
    default: return "unknown";
    }
}

static esp_err_t stop_locked(void)
{
    if (NULL == s_motion) {
        return ESP_ERR_INVALID_STATE;
    }
    return robot_motion_stop(s_motion);
}

static esp_err_t wait_for_imu_stable(uint64_t command_epoch,
                                     uint32_t *notify_value)
{
    if (NULL == notify_value) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t stable_windows = 0U;
    for (uint32_t window = 0U; window < COMPANION_IMU_SETTLE_MAX_WINDOWS;
         ++window) {
        companion_imu_sample_t samples[COMPANION_IMU_SETTLE_WINDOW_SAMPLES] = {0};
        for (uint32_t index = 0U; index < COMPANION_IMU_SETTLE_WINDOW_SAMPLES;
             ++index) {
            (void)xTaskNotifyWait(0U, UINT32_MAX, notify_value,
                                  pdMS_TO_TICKS(COMPANION_IMU_SAMPLE_MS));
            if (motion_cancelled(COMPANION_MOTION_ROLE_WAKE_TURN,
                                 command_epoch)) {
                return ESP_ERR_INVALID_STATE;
            }
            bmi260_raw_data_t accel = {0};
            bmi260_raw_data_t gyro = {0};
            esp_err_t result = bmi260_read_accel(&accel);
            if (ESP_OK == result) {
                result = bmi260_read_gyro(&gyro);
            }
            if (ESP_OK != result) {
                return result;
            }
            samples[index] = (companion_imu_sample_t){
                .gyro_x_raw = gyro.x,
                .gyro_y_raw = gyro.y,
                .gyro_z_raw = gyro.z,
                .accel_x_raw = accel.x,
                .accel_y_raw = accel.y,
                .accel_z_raw = accel.z,
                .accel_magnitude_raw = sqrtf((float)accel.x * accel.x +
                                             (float)accel.y * accel.y +
                                             (float)accel.z * accel.z),
            };
        }
        if (companion_imu_samples_stable(
                samples, COMPANION_IMU_SETTLE_WINDOW_SAMPLES,
                COMPANION_IMU_SETTLE_GYRO_RANGE_RAW,
                COMPANION_IMU_SETTLE_ACCEL_RANGE_RAW)) {
            stable_windows++;
            if (stable_windows >= COMPANION_IMU_SETTLE_REQUIRED_WINDOWS) {
                return ESP_OK;
            }
        } else {
            stable_windows = 0U;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t calibrate_imu(companion_imu_calibration_t *calibration,
                               uint64_t command_epoch,
                               uint32_t *notify_value)
{
    if (NULL == calibration || NULL == notify_value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = bmi260_init();
    if (ESP_OK != result) {
        return result;
    }
    result = wait_for_imu_stable(command_epoch, notify_value);
    if (ESP_OK != result) {
        return result;
    }
    for (uint32_t attempt = 0U; attempt < COMPANION_IMU_CALIBRATION_ATTEMPTS;
         ++attempt) {
        companion_imu_sample_t samples[COMPANION_IMU_CALIBRATION_SAMPLES] = {0};
        for (uint32_t sample = 0U; sample < COMPANION_IMU_CALIBRATION_SAMPLES;
             ++sample) {
            (void)xTaskNotifyWait(0U, UINT32_MAX, notify_value,
                                  pdMS_TO_TICKS(COMPANION_IMU_SAMPLE_MS));
            if (motion_cancelled(COMPANION_MOTION_ROLE_WAKE_TURN,
                                 command_epoch)) {
                return ESP_ERR_INVALID_STATE;
            }
            bmi260_raw_data_t accel = {0};
            bmi260_raw_data_t gyro = {0};
            result = bmi260_read_accel(&accel);
            if (ESP_OK == result) {
                result = bmi260_read_gyro(&gyro);
            }
            if (ESP_OK != result) {
                return result;
            }
            samples[sample] = (companion_imu_sample_t){
                .gyro_x_raw = gyro.x,
                .gyro_y_raw = gyro.y,
                .gyro_z_raw = gyro.z,
                .accel_x_raw = accel.x,
                .accel_y_raw = accel.y,
                .accel_z_raw = accel.z,
                .accel_magnitude_raw = sqrtf((float)accel.x * accel.x +
                                             (float)accel.y * accel.y +
                                             (float)accel.z * accel.z),
            };
        }
        result = companion_imu_estimate_calibration(
            samples, COMPANION_IMU_CALIBRATION_SAMPLES,
            COMPANION_IMU_CALIBRATION_TRIM_SAMPLES,
            COMPANION_IMU_CALIBRATION_MAX_RANGE_RAW, calibration);
        if (ESP_OK == result) {
            ESP_LOGI(TAG,
                     "IMU calibration accepted attempt=%lu bias=[%.1f,%.1f,%.1f] gravity=[%.3f,%.3f,%.3f]",
                     (unsigned long)(attempt + 1U),
                     (double)calibration->gyro_bias_raw[0],
                     (double)calibration->gyro_bias_raw[1],
                     (double)calibration->gyro_bias_raw[2],
                     (double)calibration->gravity_unit[0],
                     (double)calibration->gravity_unit[1],
                     (double)calibration->gravity_unit[2]);
            return ESP_OK;
        }
    }
    return result;
}

static esp_err_t run_wake_turn(const companion_motion_command_t *command,
                               uint64_t command_epoch,
                               uint32_t *notify_value, bool *motion_fault)
{
    if (NULL == command || NULL == notify_value || NULL == motion_fault) {
        return ESP_ERR_INVALID_ARG;
    }
    *motion_fault = false;
    companion_imu_calibration_t calibration = {0};
    esp_err_t result = calibrate_imu(&calibration, command_epoch,
                                     notify_value);
    if (ESP_OK != result) {
        ESP_LOGW(TAG, "wake turn IMU preparation failed: %s",
                 esp_err_to_name(result));
        return result;
    }
    companion_turn_control_config_t control_config = {0};
    companion_turn_control_config_default(&control_config);
    control_config.hard_timeout_ms = command->duration_ms;
    companion_turn_control_t control = {0};
    result = companion_turn_control_start(&control, &control_config,
                                          fabsf(command->target_deg),
                                          &calibration);
    if (ESP_OK != result) {
        return result;
    }
    ESP_LOGI(TAG,
             "imu turn start target=%.1f stop_lead=%.1f hard_timeout=%lums gravity=[%.3f,%.3f,%.3f]",
             (double)fabsf(command->target_deg),
             (double)control_config.stop_lead_deg,
             (unsigned long)control_config.hard_timeout_ms,
             (double)calibration.gravity_unit[0],
             (double)calibration.gravity_unit[1],
             (double)calibration.gravity_unit[2]);
    int last_reported_remaining_deg =
        (int)ceilf(companion_turn_control_remaining_deg(&control));
    if (NULL != s_config.on_progress) {
        const companion_motion_progress_t progress = {
            .target_deg = control.target_deg,
            .turned_deg = 0.0f,
            .remaining_deg = control.target_deg,
        };
        s_config.on_progress(command, &progress, s_config.user_ctx);
    }
    int left = 0;
    int right = 0;
    result = companion_action_tracks(command->action, &left, &right);
    if (ESP_OK == result && pdTRUE == xSemaphoreTake(s_lock, portMAX_DELAY)) {
        const bool cancelled = motion_cancelled(command->role, command_epoch);
        result = cancelled ? ESP_ERR_INVALID_STATE :
                 robot_motion_set_track_speed(s_motion, left, right);
        xSemaphoreGive(s_lock);
        *motion_fault = ESP_OK != result && !cancelled;
    }
    int64_t previous_sample_us = esp_timer_get_time();
    uint32_t sample_count = 0U;
    while (ESP_OK == result) {
        (void)xTaskNotifyWait(0U, UINT32_MAX, notify_value,
                              pdMS_TO_TICKS(COMPANION_IMU_SAMPLE_MS));
        if (motion_cancelled(command->role, command_epoch)) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        bmi260_raw_data_t accel = {0};
        bmi260_raw_data_t gyro = {0};
        result = bmi260_read_accel(&accel);
        if (ESP_OK == result) {
            result = bmi260_read_gyro(&gyro);
        }
        if (ESP_OK != result) {
            break;
        }
        const int64_t sample_us = esp_timer_get_time();
        const int64_t elapsed_us = sample_us - previous_sample_us;
        previous_sample_us = sample_us;
        if (0 >= elapsed_us || UINT32_MAX < (uint64_t)elapsed_us) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        bool complete = false;
        const companion_imu_sample_t imu_sample = {
            .gyro_x_raw = gyro.x,
            .gyro_y_raw = gyro.y,
            .gyro_z_raw = gyro.z,
            .accel_x_raw = accel.x,
            .accel_y_raw = accel.y,
            .accel_z_raw = accel.z,
            .accel_magnitude_raw = sqrtf((float)accel.x * accel.x +
                                         (float)accel.y * accel.y +
                                         (float)accel.z * accel.z),
        };
        result = companion_turn_control_update_sample_us(
            &control, &control_config, &imu_sample,
            (uint32_t)elapsed_us, &complete);
        const float remaining_deg =
            companion_turn_control_remaining_deg(&control);
        sample_count++;
        if (0U == (sample_count % COMPANION_IMU_PROGRESS_LOG_SAMPLES)) {
            ESP_LOGI(TAG,
                     "imu turn feedback gyro=[%d,%d,%d] projected=%.1fdps turned=%.1fdeg remaining=%.1fdeg dt=%ldus",
                     gyro.x, gyro.y, gyro.z,
                     (double)control.projected_rate_dps,
                     (double)control.turned_deg, (double)remaining_deg,
                     (long)elapsed_us);
        }
        const int reported_remaining_deg = (int)ceilf(remaining_deg);
        if (!complete && NULL != s_config.on_progress &&
            reported_remaining_deg != last_reported_remaining_deg) {
            const companion_motion_progress_t progress = {
                .target_deg = control.target_deg,
                .turned_deg = control.turned_deg,
                .remaining_deg = remaining_deg,
            };
            s_config.on_progress(command, &progress, s_config.user_ctx);
            last_reported_remaining_deg = reported_remaining_deg;
        }
        if (complete) {
            ESP_LOGI(TAG,
                     "imu turn complete target=%.1f turned=%.1f remaining=%.1f elapsed=%lums",
                     (double)fabsf(command->target_deg),
                     (double)control.turned_deg, (double)remaining_deg,
                     (unsigned long)control.elapsed_ms);
            break;
        }
    }
    return result;
}

static void motion_task(void *arg)
{
    (void)arg;
    while (true) {
        motion_queue_item_t item = {0};
        if (pdTRUE != xQueueReceive(
                s_queue, &item,
                pdMS_TO_TICKS(COMPANION_MERIT_SAMPLE_PERIOD_MS))) {
            poll_merit_tap();
            continue;
        }
        const companion_motion_command_t command = item.command;

        if (COMPANION_MOTION_ROLE_ROAM == command.role) {
            suppress_merit_tap(COMPANION_MERIT_MOTION_START_SUPPRESS_US,
                               "motion_start");
        }

        portENTER_CRITICAL(&s_state_lock);
        s_active_role = command.role;
        s_active_role_epoch = item.role_epoch;
        s_active_role_valid = true;
        portEXIT_CRITICAL(&s_state_lock);

        uint32_t notify_value = 0U;
        (void)xTaskNotifyWait(0U, UINT32_MAX, &notify_value, 0U);
        int left = 0;
        int right = 0;
        esp_err_t result = motion_cancelled(command.role, item.role_epoch) ?
                           ESP_ERR_INVALID_STATE :
                           companion_action_tracks(command.action, &left,
                                                   &right);
        ESP_LOGI(TAG,
                 "execute action=%s role=%d duration=%lu target=%.1f left=%d right=%d generation=%lu wake_seq=%lu result=%s",
                 action_name(command.action), (int)command.role,
                 (unsigned long)command.duration_ms,
                 (double)command.target_deg, left, right,
                 (unsigned long)command.generation,
                 (unsigned long)command.wake_seq, esp_err_to_name(result));

        bool motion_fault = false;
        esp_err_t stop_result = ESP_OK;
        if (COMPANION_MOTION_ROLE_WAKE_TURN == command.role) {
            if (ESP_OK == result) {
                result = run_wake_turn(&command, item.role_epoch,
                                       &notify_value, &motion_fault);
            }
        } else if (ESP_OK == result &&
                   pdTRUE == xSemaphoreTake(s_lock, portMAX_DELAY)) {
            const bool cancelled = motion_cancelled(command.role,
                                                     item.role_epoch);
            result = cancelled ? ESP_ERR_INVALID_STATE :
                     robot_motion_set_track_speed(s_motion, left, right);
            xSemaphoreGive(s_lock);
            motion_fault = ESP_OK != result && !cancelled;
        }

        if (ESP_OK == result &&
            COMPANION_MOTION_ROLE_WAKE_TURN != command.role) {
            result = wait_roam_duration(&command, item.role_epoch,
                                        &notify_value);
        }

        const bool cancelled = motion_cancelled(command.role,
                                                 item.role_epoch);

        if (pdTRUE == xSemaphoreTake(s_lock, portMAX_DELAY)) {
            stop_result = stop_locked();
            xSemaphoreGive(s_lock);
            if (ESP_OK != stop_result) {
                motion_fault = true;
                set_available(false);
            }
            if (ESP_OK == result) {
                result = stop_result;
            }
        }
        if (COMPANION_MOTION_ROLE_ROAM == command.role) {
            suppress_merit_tap(COMPANION_MERIT_MOTION_STOP_SUPPRESS_US,
                               "motion_stop");
        }
        portENTER_CRITICAL(&s_state_lock);
        if (s_active_role_valid && s_active_role == command.role &&
            s_active_role_epoch == item.role_epoch) {
            s_active_role_valid = false;
        }
        portEXIT_CRITICAL(&s_state_lock);
        if (ESP_OK != result) {
            const bool retryable_wake_turn =
                COMPANION_MOTION_ROLE_WAKE_TURN == command.role &&
                !motion_fault && !cancelled;
            if (!cancelled && !retryable_wake_turn) {
                set_available(false);
            }
            ESP_LOGE(TAG,
                     "motion failure action=%s role=%d error=%s motion_fault=%u retryable=%u",
                     action_name(command.action), (int)command.role,
                     esp_err_to_name(result), motion_fault ? 1U : 0U,
                     retryable_wake_turn ? 1U : 0U);
        }

        companion_motion_result_t completion = {
            .error = result,
            .stop_error = stop_result,
            .classification = COMPANION_MOTION_RESULT_SUCCESS,
            .available = companion_motion_is_available(),
        };
        if (motion_fault || ESP_OK != stop_result) {
            completion.classification =
                COMPANION_MOTION_RESULT_PERMANENT_OUTPUT_FAILURE;
        } else if (cancelled) {
            completion.classification = COMPANION_MOTION_RESULT_CANCELLED;
        } else if (ESP_OK != result &&
                   COMPANION_MOTION_ROLE_WAKE_TURN == command.role &&
                   !motion_fault) {
            completion.classification =
                COMPANION_MOTION_RESULT_RETRYABLE_SENSOR_FAILURE;
        } else if (ESP_OK != result) {
            completion.classification =
                COMPANION_MOTION_RESULT_PERMANENT_OUTPUT_FAILURE;
        }
        completion.available = companion_motion_is_available();

        ESP_LOGI(TAG,
                 "complete action=%s interrupted=%u generation=%lu wake_seq=%lu result=%s",
                 action_name(command.action),
                 cancelled ? 1U : 0U,
                 (unsigned long)command.generation,
                 (unsigned long)command.wake_seq, esp_err_to_name(result));
        if (NULL != s_config.on_done) {
            s_config.on_done(&command, &completion, s_config.user_ctx);
        }
    }
}

esp_err_t companion_motion_start(const companion_motion_config_t *config)
{
    if (NULL == config || NULL == config->on_done) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_state_lock);
    const bool started = s_started;
    const bool starting = s_starting;
    const bool available = s_available;
    if (!started && !starting) {
        s_starting = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (started) {
        return available ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (starting) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ESP_OK != companion_merit_tap_init(&s_merit_detector,
                                           &config->merit_tap_config)) {
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    esp_err_t result = board_laiwfs300_motor_init();
    if (ESP_OK != result) {
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGE(TAG, "motor init failed: %s", esp_err_to_name(result));
        return result;
    }
    s_motion = board_laiwfs300_motion();
    if (NULL == s_motion) {
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_lock = xSemaphoreCreateMutex();
    s_command_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(COMPANION_MOTION_QUEUE_DEPTH,
                           sizeof(motion_queue_item_t));
    if (NULL == s_lock || NULL == s_command_lock || NULL == s_queue) {
        if (NULL != s_queue) {
            vQueueDelete(s_queue);
            s_queue = NULL;
        }
        if (NULL != s_command_lock) {
            vSemaphoreDelete(s_command_lock);
            s_command_lock = NULL;
        }
        if (NULL != s_lock) {
            vSemaphoreDelete(s_lock);
            s_lock = NULL;
        }
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_NO_MEM;
    }
    BaseType_t task_result = xTaskCreatePinnedToCore(
        motion_task, "companion_motion", COMPANION_MOTION_TASK_STACK, NULL,
        COMPANION_MOTION_TASK_PRIORITY, &s_task, 1);
    if (pdPASS != task_result) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        vSemaphoreDelete(s_command_lock);
        s_command_lock = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_task = NULL;
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&s_state_lock);
    s_started = true;
    s_starting = false;
    s_available = true;
    s_role_epochs[COMPANION_MOTION_ROLE_ROAM] = 0ULL;
    s_role_epochs[COMPANION_MOTION_ROLE_WAKE_TURN] = 0ULL;
    s_active_role_valid = false;
    s_merit_gate_enabled = false;
    s_merit_gate_generation = 0U;
    s_merit_gate_wake_seq = 0U;
    s_merit_gate_revision = 1U;
    s_merit_detector_revision = 0U;
    s_merit_imu_initialized = false;
    s_merit_last_error_log_us = 0ULL;
    s_merit_suppress_until_us = 0ULL;
    s_merit_diag_samples = 0U;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG,
             "[DEBUG-MERIT-CONFIG] accel_delta=%lu stability=%lu return=%lu confirm_window_us=%lu baseline_samples=%u noise_multiplier=%u cooldown_us=%lu",
             (unsigned long)s_config.merit_tap_config.accel_delta_threshold_raw,
             (unsigned long)s_config.merit_tap_config.baseline_stability_threshold_raw,
             (unsigned long)s_config.merit_tap_config.return_threshold_raw,
             (unsigned long)s_config.merit_tap_config.confirm_window_us,
             (unsigned int)s_config.merit_tap_config.baseline_window_samples,
             (unsigned int)s_config.merit_tap_config.noise_multiplier,
             (unsigned long)s_config.merit_tap_config.cooldown_us);
    ESP_LOGI(TAG, "motion capability ready; fixed outputs are 0/100 percent");
    return ESP_OK;
}

esp_err_t companion_motion_submit(const companion_motion_command_t *command)
{
    if (NULL == command || COMPANION_ACTION_STOP >= command->action ||
        COMPANION_ACTION_COUNT <= command->action ||
        !role_is_valid(command->role) ||
        0U == command->duration_ms ||
        (COMPANION_MOTION_ROLE_WAKE_TURN == command->role &&
         (!isfinite(command->target_deg) || 0.0f >= command->target_deg))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (NULL == s_command_lock ||
        pdTRUE != xSemaphoreTake(
            s_command_lock,
            pdMS_TO_TICKS(COMPANION_MOTION_COMMAND_LOCK_MS))) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    motion_queue_item_t item = {.command = *command};
    QueueHandle_t queue = NULL;
    portENTER_CRITICAL(&s_state_lock);
    if (!s_available || NULL == s_queue || NULL == s_task) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        item.role_epoch = s_role_epochs[command->role];
        queue = s_queue;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (ESP_OK == result && pdTRUE != xQueueSend(queue, &item, 0U)) {
        result = ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_command_lock);
    return result;
}

esp_err_t companion_motion_stop(const char *reason)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    TaskHandle_t task = NULL;
    const bool command_lock_taken = NULL != s_command_lock &&
        pdTRUE == xSemaphoreTake(s_command_lock, portMAX_DELAY);
    portENTER_CRITICAL(&s_state_lock);
    for (size_t role = 0U; role < COMPANION_MOTION_ROLE_COUNT; ++role) {
        s_role_epochs[role]++;
        if (0ULL == s_role_epochs[role]) {
            s_role_epochs[role] = 1ULL;
        }
    }
    task = s_task;
    portEXIT_CRITICAL(&s_state_lock);
    if (command_lock_taken) {
        if (NULL != s_queue) {
            xQueueReset(s_queue);
        }
        xSemaphoreGive(s_command_lock);
    }
    if (NULL != task) {
        (void)xTaskNotify(task, COMPANION_MOTION_NOTIFY_STOP, eSetBits);
    }
    if (NULL != s_lock && pdTRUE == xSemaphoreTake(s_lock, pdMS_TO_TICKS(100))) {
        result = stop_locked();
        xSemaphoreGive(s_lock);
    }
    if (NULL != s_motion && ESP_OK != result) {
        set_available(false);
        ESP_LOGE(TAG,
                 "safety stop failed permanently; motion capability disabled error=%s",
                 esp_err_to_name(result));
    }
    ESP_LOGI(TAG, "safety stop reason=%s result=%s",
             (NULL != reason) ? reason : "unspecified", esp_err_to_name(result));
    return result;
}

esp_err_t companion_motion_stop_role(companion_motion_role_t role,
                                     const char *reason)
{
    if (!role_is_valid(role)) {
        return ESP_ERR_INVALID_ARG;
    }
    TaskHandle_t task = NULL;
    bool active_role = false;
    const bool command_lock_taken = NULL != s_command_lock &&
        pdTRUE == xSemaphoreTake(s_command_lock, portMAX_DELAY);
    portENTER_CRITICAL(&s_state_lock);
    s_role_epochs[role]++;
    if (0ULL == s_role_epochs[role]) {
        s_role_epochs[role] = 1ULL;
    }
    active_role = s_active_role_valid && s_active_role == role;
    task = s_task;
    portEXIT_CRITICAL(&s_state_lock);
    if (command_lock_taken) {
        xSemaphoreGive(s_command_lock);
    }
    if (!active_role) {
        ESP_LOGI(TAG, "role stop role=%d active=0 reason=%s queued_tokens_invalidated=1",
                 (int)role, (NULL != reason) ? reason : "unspecified");
        return (NULL != task) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (NULL != task) {
        (void)xTaskNotify(task, COMPANION_MOTION_NOTIFY_STOP, eSetBits);
    }
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (NULL != s_lock &&
        pdTRUE == xSemaphoreTake(s_lock, pdMS_TO_TICKS(100))) {
        result = stop_locked();
        xSemaphoreGive(s_lock);
    }
    if (ESP_OK != result) {
        set_available(false);
        ESP_LOGE(TAG,
                 "role stop failed role=%d reason=%s error=%s; motion disabled",
                 (int)role, (NULL != reason) ? reason : "unspecified",
                 esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG,
                 "role stop role=%d active=1 reason=%s queued_tokens_invalidated=1 result=%s",
                 (int)role, (NULL != reason) ? reason : "unspecified",
                 esp_err_to_name(result));
    }
    return result;
}

esp_err_t companion_motion_set_merit_tap_gate(bool enabled,
                                              uint32_t generation,
                                              uint32_t wake_seq)
{
    uint32_t revision = 0U;
    portENTER_CRITICAL(&s_state_lock);
    if (!s_started || NULL == s_task) {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_merit_gate_enabled == enabled &&
        s_merit_gate_generation == (enabled ? generation : 0U) &&
        s_merit_gate_wake_seq == (enabled ? wake_seq : 0U)) {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_OK;
    }
    s_merit_gate_enabled = enabled;
    s_merit_gate_generation = enabled ? generation : 0U;
    s_merit_gate_wake_seq = enabled ? wake_seq : 0U;
    s_merit_gate_revision++;
    if (0U == s_merit_gate_revision) {
        s_merit_gate_revision = 1U;
    }
    revision = s_merit_gate_revision;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG,
             "[DEBUG-MERIT-GATE] enabled=%u generation=%lu wake_seq=%lu revision=%lu",
             enabled ? 1U : 0U, (unsigned long)(enabled ? generation : 0U),
             (unsigned long)(enabled ? wake_seq : 0U),
             (unsigned long)revision);
    return ESP_OK;
}

bool companion_motion_is_available(void)
{
    bool available = false;
    portENTER_CRITICAL(&s_state_lock);
    available = s_available;
    portEXIT_CRITICAL(&s_state_lock);
    return available;
}
