#include "pt2466_motor.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"

static bool speed_is_valid(int speed_percent)
{
    return speed_percent >= -100 && speed_percent <= 100;
}

static esp_err_t set_both_low(pt2466_motor_t *motor)
{
    ESP_RETURN_ON_ERROR(motor->config.set_input(motor->config.user_ctx, motor->config.positive_input, 0), "pt2466", "positive input low failed");
    ESP_RETURN_ON_ERROR(motor->config.set_input(motor->config.user_ctx, motor->config.negative_input, 0), "pt2466", "negative input low failed");
    return ESP_OK;
}

esp_err_t pt2466_motor_init(pt2466_motor_t *motor, const pt2466_motor_config_t *config)
{
    if (NULL == motor || NULL == config || NULL == config->set_input) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(motor, 0, sizeof(*motor));
    motor->config = *config;
    motor->initialized = true;
    return pt2466_motor_stop(motor);
}

esp_err_t pt2466_motor_set_speed(pt2466_motor_t *motor, int speed_percent)
{
    if (NULL == motor || !motor->initialized || !speed_is_valid(speed_percent)) {
        return ESP_ERR_INVALID_ARG;
    }

    int effective_speed = motor->config.invert_direction ? -speed_percent : speed_percent;
    uint16_t duty_permille = (uint16_t)(abs(effective_speed) * 10);

    ESP_RETURN_ON_ERROR(set_both_low(motor), "pt2466", "safe low before speed change failed");

    if (effective_speed > 0) {
        ESP_RETURN_ON_ERROR(motor->config.set_input(motor->config.user_ctx, motor->config.positive_input, duty_permille), "pt2466", "positive speed failed");
    } else if (effective_speed < 0) {
        ESP_RETURN_ON_ERROR(motor->config.set_input(motor->config.user_ctx, motor->config.negative_input, duty_permille), "pt2466", "negative speed failed");
    }

    motor->last_speed_percent = speed_percent;
    return ESP_OK;
}

esp_err_t pt2466_motor_stop(pt2466_motor_t *motor)
{
    if (NULL == motor || !motor->initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(set_both_low(motor), "pt2466", "stop failed");
    motor->last_speed_percent = 0;
    return ESP_OK;
}
