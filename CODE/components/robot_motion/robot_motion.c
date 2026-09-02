#include "robot_motion.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "robot_motion";

static bool speed_is_valid(int speed_percent)
{
    return speed_percent >= 0 && speed_percent <= 100;
}

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

esp_err_t robot_motion_init(robot_motion_t *motion, pt2466_motor_t *left, pt2466_motor_t *right)
{
    if (NULL == motion || NULL == left || NULL == right) {
        return ESP_ERR_INVALID_ARG;
    }

    motion->left = left;
    motion->right = right;
    motion->initialized = true;
    return robot_motion_stop(motion);
}

esp_err_t robot_motion_set_track_speed(robot_motion_t *motion, int left_percent, int right_percent)
{
    if (NULL == motion || !motion->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(pt2466_motor_set_speed(motion->left, left_percent), TAG, "left speed failed");
    ESP_RETURN_ON_ERROR(pt2466_motor_set_speed(motion->right, right_percent), TAG, "right speed failed");
    return ESP_OK;
}

esp_err_t robot_motion_stop(robot_motion_t *motion)
{
    if (NULL == motion || !motion->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(pt2466_motor_stop(motion->left), TAG, "left stop failed");
    ESP_RETURN_ON_ERROR(pt2466_motor_stop(motion->right), TAG, "right stop failed");
    return ESP_OK;
}

esp_err_t robot_motion_forward(robot_motion_t *motion, int speed_percent)
{
    if (!speed_is_valid(speed_percent)) {
        return ESP_ERR_INVALID_ARG;
    }
    return robot_motion_set_track_speed(motion, speed_percent, speed_percent);
}

esp_err_t robot_motion_backward(robot_motion_t *motion, int speed_percent)
{
    if (!speed_is_valid(speed_percent)) {
        return ESP_ERR_INVALID_ARG;
    }
    return robot_motion_set_track_speed(motion, -speed_percent, -speed_percent);
}

esp_err_t robot_motion_turn_left(robot_motion_t *motion, int speed_percent)
{
    if (!speed_is_valid(speed_percent)) {
        return ESP_ERR_INVALID_ARG;
    }
    return robot_motion_set_track_speed(motion, -speed_percent, speed_percent);
}

esp_err_t robot_motion_turn_right(robot_motion_t *motion, int speed_percent)
{
    if (!speed_is_valid(speed_percent)) {
        return ESP_ERR_INVALID_ARG;
    }
    return robot_motion_set_track_speed(motion, speed_percent, -speed_percent);
}

esp_err_t robot_motion_run_smoke_test(robot_motion_t *motion, const robot_motion_smoke_test_config_t *config)
{
    if (NULL == motion || NULL == config || !speed_is_valid(config->speed_percent)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG, "smoke test: forward %ums", (unsigned int)config->move_ms);
    ESP_RETURN_ON_ERROR(robot_motion_forward(motion, config->speed_percent), TAG, "forward failed");
    delay_ms(config->move_ms);
    ESP_RETURN_ON_ERROR(robot_motion_stop(motion), TAG, "stop after forward failed");
    delay_ms(config->stop_ms);

    ESP_LOGW(TAG, "smoke test: backward %ums", (unsigned int)config->move_ms);
    ESP_RETURN_ON_ERROR(robot_motion_backward(motion, config->speed_percent), TAG, "backward failed");
    delay_ms(config->move_ms);
    ESP_RETURN_ON_ERROR(robot_motion_stop(motion), TAG, "stop after backward failed");
    delay_ms(config->stop_ms);

    ESP_LOGW(TAG, "smoke test: turn left %ums", (unsigned int)config->move_ms);
    ESP_RETURN_ON_ERROR(robot_motion_turn_left(motion, config->speed_percent), TAG, "turn left failed");
    delay_ms(config->move_ms);
    ESP_RETURN_ON_ERROR(robot_motion_stop(motion), TAG, "stop after left turn failed");
    delay_ms(config->stop_ms);

    ESP_LOGW(TAG, "smoke test: turn right %ums", (unsigned int)config->move_ms);
    ESP_RETURN_ON_ERROR(robot_motion_turn_right(motion, config->speed_percent), TAG, "turn right failed");
    delay_ms(config->move_ms);

    return robot_motion_stop(motion);
}
