/**
 * @file rc_control.c
 * @brief RC Tank Demo - 连续角度/力度控制层
 */

#include "rc_control.h"
#include "rc_net.h"
#include "board_laiwfs300.h"
#include "robot_motion.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "rc_control";

#define RC_CONTROL_TASK_PERIOD_MS 20U

#if defined(CONFIG_RC_TANK_ROLE_TANK)

#include "rc_drive_control.h"
#include "rc_dvp_staged_override.h"

#define RC_CTRL_RX_ACTIVE_WAIT_MS 20U
#define RC_CTRL_RX_IDLE_WAIT_MS 50U
#define RC_CTRL_INVALID_LOG_INTERVAL 64U

static robot_motion_t *s_motion = NULL;
static rc_drive_controller_t s_drive_controller = {0};
static rc_drive_output_t s_drive_output = {0};
static rc_ctrl_receiver_t s_ctrl_receiver = {0};
static TaskHandle_t s_ctrl_rx_task = NULL;
static SemaphoreHandle_t s_motor_mutex = NULL;
static bool s_drive_initialized = false;
static bool s_tank_network_connected = false;

static void stop_motor_locked(void)
{
    if (s_drive_initialized) {
        rc_drive_controller_stop(&s_drive_controller, &s_drive_output);
    }
    if (NULL != s_motion) {
        robot_motion_stop(s_motion);
    }
}

static void apply_command_locked(const rc_ctrl_command_t *command)
{
    if (RC_CTRL_MODE_STOP == command->mode) {
        stop_motor_locked();
    } else {
        rc_drive_controller_set_target(&s_drive_controller, command);
    }
}

static void apply_drive_step_locked(void)
{
    if (!s_drive_initialized || (NULL == s_motion)) {
        return;
    }
    if (rc_drive_controller_step(&s_drive_controller, &s_drive_output)) {
        const esp_err_t result = robot_motion_set_track_speed(
            s_motion,
            s_drive_output.left_pwm_pct,
            s_drive_output.right_pwm_pct);
        if (ESP_OK != result) {
            ESP_LOGE(TAG, "Track output failed: %s", esp_err_to_name(result));
            rc_drive_controller_stop(&s_drive_controller, &s_drive_output);
            robot_motion_stop(s_motion);
        }
    }
}

esp_err_t rc_motor_init(void)
{
    rc_drive_config_t config = {0};
    esp_err_t result = board_laiwfs300_motor_init();

    if (ESP_OK != result) {
        ESP_LOGE(TAG, "Motor init failed: %s", esp_err_to_name(result));
        return result;
    }
    s_motion = board_laiwfs300_motion();
    if (NULL == s_motion) {
        ESP_LOGE(TAG, "Motion handle is NULL");
        return ESP_FAIL;
    }

    s_motor_mutex = xSemaphoreCreateMutex();
    if (NULL == s_motor_mutex) {
        ESP_LOGE(TAG, "Motor mutex allocation failed");
        robot_motion_stop(s_motion);
        return ESP_ERR_NO_MEM;
    }

    robot_motion_stop(s_motion);
    rc_drive_config_set_defaults(&config);
    if (!rc_drive_controller_init(&s_drive_controller, &config)) {
        ESP_LOGE(TAG, "Drive configuration is invalid");
        robot_motion_stop(s_motion);
        vSemaphoreDelete(s_motor_mutex);
        s_motor_mutex = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    s_drive_initialized = true;
    rc_ctrl_receiver_reset(&s_ctrl_receiver);
    ESP_LOGI(TAG, "Motor initialized for continuous track control");
    return ESP_OK;
}

void rc_motor_apply(const rc_ctrl_command_t *command)
{
    if ((NULL == command) || !s_drive_initialized) {
        return;
    }
    if ((NULL == s_motor_mutex) ||
        (pdTRUE != xSemaphoreTake(s_motor_mutex, portMAX_DELAY))) {
        return;
    }
    apply_command_locked(command);
    xSemaphoreGive(s_motor_mutex);
}

void rc_motor_stop(void)
{
    if ((NULL != s_motor_mutex) &&
        (pdTRUE != xSemaphoreTake(s_motor_mutex, portMAX_DELAY))) {
        return;
    }
    stop_motor_locked();
    if (NULL != s_motor_mutex) {
        xSemaphoreGive(s_motor_mutex);
    }
}

void rc_control_set_network_connected(bool connected)
{
    if ((NULL == s_motor_mutex) ||
        (pdTRUE != xSemaphoreTake(s_motor_mutex, portMAX_DELAY))) {
        s_tank_network_connected = connected;
        return;
    }
    s_tank_network_connected = connected;
    if (!s_tank_network_connected) {
        stop_motor_locked();
        rc_ctrl_receiver_reset(&s_ctrl_receiver);
    }
    xSemaphoreGive(s_motor_mutex);
    ESP_LOGI(TAG, "Network %s", connected ? "connected" : "disconnected: motor stopped");
}

static void ctrl_rx_task(void *arg)
{
    uint8_t wire[RC_CTRL_PACKET_SIZE + 1U] = {0};
    uint32_t rejected_packets = 0U;
    bool drive_active = false;

    (void)arg;
    ESP_LOGI(TAG, "Ctrl RX task started");

    while (1) {
        size_t wire_size = 0U;
        rc_ctrl_command_t command = {0};
        bool packet_rejected = false;
        bool timed_out = false;
        const uint32_t rx_wait_ms = drive_active
            ? RC_CTRL_RX_ACTIVE_WAIT_MS
            : RC_CTRL_RX_IDLE_WAIT_MS;
        const esp_err_t result = rc_net_ctrl_recv(wire, sizeof(wire),
                                                   &wire_size,
                                                   rx_wait_ms);
        const uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if ((NULL != s_motor_mutex) &&
            (pdTRUE == xSemaphoreTake(s_motor_mutex, portMAX_DELAY))) {
            if (s_tank_network_connected) {
                if (ESP_OK == result) {
                    if (rc_ctrl_receiver_accept_wire(&s_ctrl_receiver,
                                                     wire,
                                                     wire_size,
                                                     now_ms,
                                                     &command)) {
                        rc_dvp_staged_note_control_rx();
                        drive_active =
                            (RC_CTRL_MODE_DRIVE == command.mode);
                        apply_command_locked(&command);
                    } else {
                        packet_rejected = true;
                    }
                }
                timed_out = rc_ctrl_receiver_is_timed_out(&s_ctrl_receiver,
                                                          now_ms);
                if (timed_out) {
                    drive_active = false;
                    rc_ctrl_receiver_reset(&s_ctrl_receiver);
                    stop_motor_locked();
                }
                apply_drive_step_locked();
            } else {
                drive_active = false;
            }
            xSemaphoreGive(s_motor_mutex);
        }

        if (packet_rejected) {
            ++rejected_packets;
            if (1U == (rejected_packets % RC_CTRL_INVALID_LOG_INTERVAL)) {
                ESP_LOGW(TAG, "Rejected control packets=%u",
                         (unsigned)rejected_packets);
            }
        }
        if (timed_out) {
            ESP_LOGW(TAG, "Control timeout: motor stop enforced");
        }
        if (ESP_ERR_INVALID_STATE == result) {
            vTaskDelay(pdMS_TO_TICKS(RC_CTRL_HEARTBEAT_MS));
        } else if ((ESP_OK != result) && (ESP_ERR_TIMEOUT != result)) {
            ESP_LOGW(TAG, "Control receive failed: %s",
                     esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(RC_CTRL_RX_IDLE_WAIT_MS));
        }
    }
}

esp_err_t rc_control_start_tank(void)
{
    if (NULL != s_ctrl_rx_task) {
        ESP_LOGW(TAG, "Ctrl RX task already running");
        return ESP_OK;
    }

    rc_motor_stop();
    if ((NULL != s_motor_mutex) &&
        (pdTRUE == xSemaphoreTake(s_motor_mutex, portMAX_DELAY))) {
        rc_ctrl_receiver_reset(&s_ctrl_receiver);
        xSemaphoreGive(s_motor_mutex);
    }
    if (pdPASS != xTaskCreate(ctrl_rx_task, "ctrl_rx", 3072, NULL,
                              configMAX_PRIORITIES - 2, &s_ctrl_rx_task)) {
        ESP_LOGE(TAG, "Failed to create ctrl_rx task");
        s_ctrl_rx_task = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Tank control started");
    return ESP_OK;
}

#endif

#if defined(CONFIG_RC_TANK_ROLE_REMOTE)

#include "touch_hal.h"
#include "rc_joystick.h"
#include "rc_control_tx_policy.h"

#define RC_CTRL_TX_TASK_STACK_BYTES 4096U

typedef struct {
    int knob_dx;
    int knob_dy;
    bool active;
} rc_joystick_state_t;

static rc_joystick_state_t s_joystick_state = {0};
static rc_ctrl_command_t s_current_command = {
    .mode = RC_CTRL_MODE_STOP,
    .angle_deg = 0,
    .magnitude_pct = 0U,
};
static TaskHandle_t s_ctrl_tx_task = NULL;
static TaskHandle_t s_touch_task = NULL;
static portMUX_TYPE s_control_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_remote_network_connected = false;
static bool s_remote_release_required = true;
static uint32_t s_remote_tx_epoch = 0U;

static rc_ctrl_command_t stop_command(void)
{
    return (rc_ctrl_command_t){
        .mode = RC_CTRL_MODE_STOP,
        .angle_deg = 0,
        .magnitude_pct = 0U,
    };
}

void rc_control_set_network_connected(bool connected)
{
    bool changed = false;

    portENTER_CRITICAL(&s_control_lock);
    changed = (connected != s_remote_network_connected);
    s_remote_network_connected = connected;
    if (!connected) {
        s_remote_release_required = true;
    }
    if (changed) {
        ++s_remote_tx_epoch;
    }
    portEXIT_CRITICAL(&s_control_lock);

    if (connected) {
        ESP_LOGI(TAG, "Network connected: waiting for joystick release");
    } else {
        ESP_LOGW(TAG, "Network disconnected: control TX paused");
    }
}

static void touch_poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Touch poll task started");

    while (1) {
        touch_panel_point_t point = {0};
        uint8_t touch_count = 0U;
        rc_joystick_input_t input = {0};
        rc_ctrl_command_t previous_command = {0};
        const esp_err_t result = touch_panel_read_point(&point, &touch_count);

        if ((ESP_OK == result) && (0U < touch_count)) {
            rc_joystick_resolve_touch(true, (int)point.x, (int)point.y,
                                      &input);
        } else {
            rc_joystick_resolve_touch(false, 0, 0, &input);
        }

        portENTER_CRITICAL(&s_control_lock);
        previous_command = s_current_command;
        s_joystick_state.knob_dx = input.knob_dx;
        s_joystick_state.knob_dy = input.knob_dy;
        s_joystick_state.active = input.active;
        s_current_command = input.command;
        portEXIT_CRITICAL(&s_control_lock);

        if (!rc_control_tx_commands_equal(&previous_command,
                                          &input.command)) {
            ESP_LOGI(TAG,
                     "Joystick raw=(%d,%d) active=%d offset=(%d,%d) mode=%u angle=%d magnitude=%u",
                     (int)point.x, (int)point.y, input.active,
                     input.knob_dx, input.knob_dy,
                     (unsigned)input.command.mode,
                     (int)input.command.angle_deg,
                     (unsigned)input.command.magnitude_pct);
        }
        vTaskDelay(pdMS_TO_TICKS(RC_CONTROL_TASK_PERIOD_MS));
    }
}

esp_err_t rc_joystick_init(void *parent)
{
    esp_err_t result = ESP_OK;

    (void)parent;
    result = board_laiwfs300_touch_init();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(result));
        return result;
    }
    result = board_laiwfs300_touch_verify();
    if (ESP_OK != result) {
        ESP_LOGW(TAG, "Touch verify failed (continue anyway)");
    }
    ESP_LOGI(TAG,
             "Joystick init (center=%d,%d, continuous angle/magnitude, 50Hz)",
             RC_JOY_BASE_CX, RC_JOY_BASE_CY);
    return ESP_OK;
}

void rc_joystick_get_command(rc_ctrl_command_t *command)
{
    if (NULL == command) {
        return;
    }
    portENTER_CRITICAL(&s_control_lock);
    *command = s_current_command;
    portEXIT_CRITICAL(&s_control_lock);
}

void rc_joystick_get_state(int *knob_dx, int *knob_dy, bool *active)
{
    rc_joystick_state_t state = {0};

    portENTER_CRITICAL(&s_control_lock);
    state = s_joystick_state;
    portEXIT_CRITICAL(&s_control_lock);
    if (NULL != knob_dx) *knob_dx = state.knob_dx;
    if (NULL != knob_dy) *knob_dy = state.knob_dy;
    if (NULL != active) *active = state.active;
}

static void ctrl_tx_task(void *arg)
{
    rc_control_tx_policy_t policy = {0};
    uint32_t observed_epoch = UINT32_MAX;
    uint32_t tx_success = 0U;
    uint32_t tx_stop_success = 0U;
    uint32_t tx_drive_success = 0U;
    uint32_t tx_failures = 0U;
    uint32_t last_summary_ms = 0U;

    (void)arg;
    ESP_LOGI(TAG, "Ctrl TX task started (V1 idle STOP-once)");

    while (1) {
        rc_ctrl_command_t command = {0};
        bool connected = false;
        bool release_required = true;
        bool joystick_active = false;
        uint32_t tx_epoch = 0U;
        const uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        portENTER_CRITICAL(&s_control_lock);
        command = s_current_command;
        connected = s_remote_network_connected;
        release_required = s_remote_release_required;
        joystick_active = s_joystick_state.active;
        tx_epoch = s_remote_tx_epoch;
        portEXIT_CRITICAL(&s_control_lock);

        if (observed_epoch != tx_epoch) {
            rc_control_tx_reset(&policy);
            observed_epoch = tx_epoch;
        }
        if (!connected) {
            vTaskDelay(pdMS_TO_TICKS(RC_CONTROL_TASK_PERIOD_MS));
            continue;
        }

        if (release_required) {
            if (!joystick_active && (RC_CTRL_MODE_STOP == command.mode)) {
                portENTER_CRITICAL(&s_control_lock);
                s_remote_release_required = false;
                portEXIT_CRITICAL(&s_control_lock);
                ESP_LOGI(TAG, "Joystick released: motion control re-armed");
            }
            command = stop_command();
        }

        if (rc_control_tx_should_send(&policy, &command, now_ms)) {
            const rc_ctrl_packet_t packet = {
                .mode = command.mode,
                .angle_deg = command.angle_deg,
                .magnitude_pct = command.magnitude_pct,
                .seq = rc_control_tx_sequence(&policy),
                .sender_time_ms = now_ms,
            };
            uint8_t wire[RC_CTRL_PACKET_SIZE] = {0};

            if (!rc_ctrl_packet_encode(&packet, wire, sizeof(wire))) {
                ESP_LOGE(TAG, "Control packet encode failed");
            } else {
                const esp_err_t result = rc_net_ctrl_send(wire, sizeof(wire));
                if (ESP_OK == result) {
                    rc_control_tx_mark_sent(&policy, &command, now_ms);
                    ++tx_success;
                    if (RC_CTRL_MODE_STOP == command.mode) {
                        ++tx_stop_success;
                    } else {
                        ++tx_drive_success;
                    }
                } else {
                    ++tx_failures;
                    ESP_LOGW(TAG, "Ctrl send failed: %s stack_hwm=%u",
                             esp_err_to_name(result),
                             (unsigned)uxTaskGetStackHighWaterMark(NULL));
                }
            }
        }
        if ((uint32_t)(now_ms - last_summary_ms) >= 8000U) {
            ESP_LOGI(TAG,
                     "[DEBUG-DVP-LIMIT] ctrl_tx ok=%lu stop=%lu drive=%lu fail=%lu current=%s",
                     (unsigned long)tx_success,
                     (unsigned long)tx_stop_success,
                     (unsigned long)tx_drive_success,
                     (unsigned long)tx_failures,
                     (RC_CTRL_MODE_STOP == command.mode) ? "STOP" : "DRIVE");
            last_summary_ms = now_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(RC_CONTROL_TASK_PERIOD_MS));
    }
}

esp_err_t rc_control_start_remote(void)
{
    if (NULL != s_ctrl_tx_task) {
        ESP_LOGW(TAG, "Ctrl TX task already running");
        return ESP_OK;
    }
    if (NULL == s_touch_task) {
        if (pdPASS != xTaskCreate(touch_poll_task, "touch_poll", 3072, NULL,
                                  configMAX_PRIORITIES - 2, &s_touch_task)) {
            ESP_LOGE(TAG, "Failed to create touch_poll task");
            s_touch_task = NULL;
            return ESP_FAIL;
        }
    }
    if (pdPASS != xTaskCreate(ctrl_tx_task, "ctrl_tx",
                              RC_CTRL_TX_TASK_STACK_BYTES, NULL,
                              configMAX_PRIORITIES - 2, &s_ctrl_tx_task)) {
        ESP_LOGE(TAG, "Failed to create ctrl_tx task");
        s_ctrl_tx_task = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Remote control started (touch poll + V1 ctrl tx)");
    return ESP_OK;
}

#endif

#if defined(CONFIG_RC_TANK_ROLE_TANK)
esp_err_t rc_joystick_init(void *parent)
{
    (void)parent;
    return ESP_ERR_NOT_SUPPORTED;
}
void rc_joystick_get_command(rc_ctrl_command_t *command)
{
    if (NULL != command) {
        *command = (rc_ctrl_command_t){.mode = RC_CTRL_MODE_STOP};
    }
}
esp_err_t rc_control_start_remote(void) { return ESP_ERR_NOT_SUPPORTED; }
#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)
esp_err_t rc_motor_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void rc_motor_apply(const rc_ctrl_command_t *command) { (void)command; }
void rc_motor_stop(void) { }
esp_err_t rc_control_start_tank(void) { return ESP_ERR_NOT_SUPPORTED; }
#endif
