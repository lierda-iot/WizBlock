#include "board_laiwfs300.h"
#include "robot_motion.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "motor_demo";

#define MOTOR_SPEED_PERCENT 100
#define MOTOR_ACTION_DURATION_MS 3000
#define MOTOR_STOP_DURATION_MS 1000

typedef esp_err_t (*motor_action_fn_t)(robot_motion_t *motion, int speed_percent);

static esp_err_t run_motor_action(robot_motion_t *motion, const char *action_name, motor_action_fn_t action)
{
    if (NULL == motion || NULL == action_name || NULL == action) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "%s at %d%%", action_name, MOTOR_SPEED_PERCENT);
    esp_err_t ret = action(motion, MOTOR_SPEED_PERCENT);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "%s failed: %s", action_name, esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(MOTOR_ACTION_DURATION_MS));

    ret = robot_motion_stop(motion);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "stop after %s failed: %s", action_name, esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(MOTOR_STOP_DURATION_MS));
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Motor demo starting (PT2466 dual-track)");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = board_laiwfs300_motor_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "motor init failed: %s", esp_err_to_name(ret));
        return;
    }

    robot_motion_t *motion = board_laiwfs300_motion();
    if (NULL == motion) {
        ESP_LOGE(TAG, "motion handle is NULL");
        return;
    }

    ESP_LOGI(TAG, "starting motion sequence");
    while (true) {
        if (ESP_OK != run_motor_action(motion, "forward", robot_motion_forward) ||
            ESP_OK != run_motor_action(motion, "backward", robot_motion_backward) ||
            ESP_OK != run_motor_action(motion, "turn left", robot_motion_turn_left) ||
            ESP_OK != run_motor_action(motion, "turn right", robot_motion_turn_right)) {
            (void)robot_motion_stop(motion);
            ESP_LOGE(TAG, "motion sequence aborted");
            return;
        }
    }
}
