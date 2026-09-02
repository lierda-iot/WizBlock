#include "board_laiwfs300.h"

#include "board_pins.h"
#include "pt2466_motor.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *TAG = "board_motor";

typedef struct {
    uint8_t input_index;
    int gpio_num;
    ledc_channel_t channel;
} motor_input_binding_t;

typedef struct {
    bool initialized;
    motor_input_binding_t inputs[4];
} board_motor_io_t;

static board_motor_io_t s_motor_io = {
    .inputs = {
        {.input_index = 1, .gpio_num = BOARD_LAIWFS300_GPIO_MOTOR_IN1, .channel = LEDC_CHANNEL_0},
        {.input_index = 2, .gpio_num = BOARD_LAIWFS300_GPIO_MOTOR_IN2, .channel = LEDC_CHANNEL_1},
        {.input_index = 3, .gpio_num = BOARD_LAIWFS300_GPIO_MOTOR_IN3, .channel = LEDC_CHANNEL_2},
        {.input_index = 4, .gpio_num = BOARD_LAIWFS300_GPIO_MOTOR_IN4, .channel = LEDC_CHANNEL_3},
    },
};

static pt2466_motor_t s_left_motor;
static pt2466_motor_t s_right_motor;
static robot_motion_t s_motion;
static bool s_motion_initialized;

static ledc_timer_bit_t motor_duty_resolution(void)
{
    return LEDC_TIMER_10_BIT;
}

static const motor_input_binding_t *find_input(uint8_t input_index)
{
    for (size_t i = 0; i < 4; ++i) {
        if (s_motor_io.inputs[i].input_index == input_index) {
            return &s_motor_io.inputs[i];
        }
    }
    return NULL;
}

static esp_err_t set_motor_input(void *ctx, uint8_t input_index, uint16_t duty_permille)
{
    (void)ctx;
    const motor_input_binding_t *input = find_input(input_index);
    if (NULL == input || duty_permille > 1000U || !s_motor_io.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t duty = ((uint32_t)duty_permille * BOARD_LAIWFS300_MOTOR_PWM_DUTY_MAX) / 1000U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, input->channel, duty), TAG, "set duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, input->channel), TAG, "update duty");
    return ESP_OK;
}

static esp_err_t configure_motor_pwm(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = motor_duty_resolution(),
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BOARD_LAIWFS300_MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "timer config");

    for (size_t i = 0; i < 4; ++i) {
        ledc_channel_config_t channel = {
            .gpio_num = s_motor_io.inputs[i].gpio_num,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = s_motor_io.inputs[i].channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "channel config");
        ESP_LOGI(TAG, "motor input %u -> GPIO%d / LEDC channel %d",
                 s_motor_io.inputs[i].input_index,
                 s_motor_io.inputs[i].gpio_num,
                 s_motor_io.inputs[i].channel);
    }

    s_motor_io.initialized = true;
    return ESP_OK;
}

esp_err_t board_laiwfs300_motor_init(void)
{
    if (s_motion_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(configure_motor_pwm(), TAG, "pwm init");

    const pt2466_motor_config_t left_config = {
        .name = "left_track",
        .positive_input = 1,
        .negative_input = 2,
        .invert_direction = true,
        .set_input = set_motor_input,
        .user_ctx = &s_motor_io,
    };
    const pt2466_motor_config_t right_config = {
        .name = "right_track",
        .positive_input = 3,
        .negative_input = 4,
        .invert_direction = true,
        .set_input = set_motor_input,
        .user_ctx = &s_motor_io,
    };

    ESP_RETURN_ON_ERROR(pt2466_motor_init(&s_left_motor, &left_config), TAG, "left motor init");
    ESP_RETURN_ON_ERROR(pt2466_motor_init(&s_right_motor, &right_config), TAG, "right motor init");
    ESP_RETURN_ON_ERROR(robot_motion_init(&s_motion, &s_left_motor, &s_right_motor), TAG, "motion init");

    s_motion_initialized = true;
    return ESP_OK;
}

robot_motion_t *board_laiwfs300_motion(void)
{
    return s_motion_initialized ? &s_motion : NULL;
}

void board_laiwfs300_motor_dump_state(void)
{
    if (!s_motor_io.initialized) {
        ESP_LOGW(TAG, "motor dump: not initialized");
        return;
    }
    for (size_t i = 0; i < 4; ++i) {
        uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, s_motor_io.inputs[i].channel);
        int gpio = s_motor_io.inputs[i].gpio_num;
        int level = gpio_get_level(gpio);
        ESP_LOGW(TAG, "motor IN%u: GPIO%d duty=%lu/%u gpio_level=%d",
                 s_motor_io.inputs[i].input_index, gpio,
                 (unsigned long)duty, BOARD_LAIWFS300_MOTOR_PWM_DUTY_MAX, level);
    }
}
