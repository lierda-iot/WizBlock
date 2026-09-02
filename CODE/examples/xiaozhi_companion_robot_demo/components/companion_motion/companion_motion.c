#include "companion_motion.h"
#include "companion_motion_result_policy.h"
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
                               uint32_t *notify_value, bool *motion_fault,
                               companion_motion_failure_stage_t *failure_stage)
{
    if (NULL == command || NULL == notify_value || NULL == motion_fault ||
        NULL == failure_stage) {
        return ESP_ERR_INVALID_ARG;
    }
    *motion_fault = false;
    *failure_stage = COMPANION_MOTION_FAILURE_NONE;
    companion_imu_calibration_t calibration = {0};
    esp_err_t result = calibrate_imu(&calibration, command_epoch,
                                     notify_value);
    if (ESP_OK != result) {
        *failure_stage = COMPANION_MOTION_FAILURE_IMU_PREPARATION;
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
        *failure_stage = COMPANION_MOTION_FAILURE_IMU_PREPARATION;
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
        if (*motion_fault) {
            *failure_stage = COMPANION_MOTION_FAILURE_OUTPUT_START;
        }
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
            *failure_stage = COMPANION_MOTION_FAILURE_FEEDBACK_TIMEOUT;
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
        if (ESP_OK != result) {
            *failure_stage =
                (COMPANION_TURN_FAILURE_STALL == control.failure_reason) ?
                    COMPANION_MOTION_FAILURE_FEEDBACK_STALL :
                    COMPANION_MOTION_FAILURE_FEEDBACK_TIMEOUT;
        }
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
        if (pdTRUE != xQueueReceive(s_queue, &item, portMAX_DELAY)) {
            continue;
        }
        const companion_motion_command_t command = item.command;

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
        companion_motion_failure_stage_t failure_stage =
            COMPANION_MOTION_FAILURE_NONE;
        esp_err_t stop_result = ESP_OK;
        if (COMPANION_MOTION_ROLE_WAKE_TURN == command.role) {
            if (ESP_OK == result) {
                result = run_wake_turn(&command, item.role_epoch,
                                       &notify_value, &motion_fault,
                                       &failure_stage);
            }
        } else if (ESP_OK == result &&
                   pdTRUE == xSemaphoreTake(s_lock, portMAX_DELAY)) {
            const bool cancelled = motion_cancelled(command.role,
                                                     item.role_epoch);
            result = cancelled ? ESP_ERR_INVALID_STATE :
                     robot_motion_set_track_speed(s_motion, left, right);
            xSemaphoreGive(s_lock);
            motion_fault = ESP_OK != result && !cancelled;
            if (motion_fault) {
                failure_stage = COMPANION_MOTION_FAILURE_OUTPUT_START;
            }
        }

        if (ESP_OK == result &&
            COMPANION_MOTION_ROLE_WAKE_TURN != command.role) {
            (void)xTaskNotifyWait(0U, UINT32_MAX, &notify_value,
                                  pdMS_TO_TICKS(command.duration_ms));
            if (motion_cancelled(command.role, item.role_epoch)) {
                result = ESP_ERR_INVALID_STATE;
            }
        }

        const bool cancelled = motion_cancelled(command.role,
                                                 item.role_epoch);

        if (pdTRUE == xSemaphoreTake(s_lock, portMAX_DELAY)) {
            stop_result = stop_locked();
            xSemaphoreGive(s_lock);
            if (ESP_OK != stop_result) {
                motion_fault = true;
                failure_stage = COMPANION_MOTION_FAILURE_STOP;
                set_available(false);
            }
            if (ESP_OK == result) {
                result = stop_result;
            }
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
            .classification = companion_motion_result_classify(
                failure_stage, cancelled, result, stop_result),
            .failure_stage = failure_stage,
            .available = companion_motion_is_available(),
        };
        completion.available = companion_motion_is_available();

        ESP_LOGI(TAG,
                 "complete action=%s interrupted=%u generation=%lu wake_seq=%lu failure_stage=%d class=%d result=%s",
                 action_name(command.action),
                 cancelled ? 1U : 0U,
                 (unsigned long)command.generation,
                 (unsigned long)command.wake_seq, (int)failure_stage,
                 (int)completion.classification, esp_err_to_name(result));
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
    portEXIT_CRITICAL(&s_state_lock);
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

bool companion_motion_is_available(void)
{
    bool available = false;
    portENTER_CRITICAL(&s_state_lock);
    available = s_available;
    portEXIT_CRITICAL(&s_state_lock);
    return available;
}
