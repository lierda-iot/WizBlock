#include "hotplug_motor_runtime.h"

#include "board_pins.h"
#include "pt2466_motor.h"
#include "robot_motion.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HOTPLUG_MOTOR_INPUT_COUNT 4U
#define HOTPLUG_MOTOR_FORWARD_PERCENT 100

static const char *TAG = "hotplug_motor";

typedef struct {
    uint8_t input_index;
    gpio_num_t gpio_num;
    ledc_channel_t channel;
} motor_input_binding_t;

typedef struct {
    bool pwm_ready;
    bool timer_configured;
    size_t configured_channel_count;
    motor_input_binding_t inputs[HOTPLUG_MOTOR_INPUT_COUNT];
} hotplug_motor_io_t;

static hotplug_motor_io_t s_motor_io = {
    .inputs = {
        {.input_index = 1U, .gpio_num = BOARD_LAIWFS300_GPIO_MOTOR_IN1, .channel = LEDC_CHANNEL_0},
        {.input_index = 2U, .gpio_num = BOARD_LAIWFS300_GPIO_MOTOR_IN2, .channel = LEDC_CHANNEL_1},
        {.input_index = 3U, .gpio_num = BOARD_LAIWFS300_GPIO_MOTOR_IN3, .channel = LEDC_CHANNEL_2},
        {.input_index = 4U, .gpio_num = BOARD_LAIWFS300_GPIO_MOTOR_IN4, .channel = LEDC_CHANNEL_3},
    },
};
static pt2466_motor_t s_left_motor;
static pt2466_motor_t s_right_motor;
static robot_motion_t s_motion;
static bool s_runtime_initialized;

static const motor_input_binding_t *find_input(const hotplug_motor_io_t *io,
                                                uint8_t input_index)
{
    if (NULL == io) {
        return NULL;
    }
    for (size_t i = 0U; i < HOTPLUG_MOTOR_INPUT_COUNT; i++) {
        if (input_index == io->inputs[i].input_index) {
            return &io->inputs[i];
        }
    }
    return NULL;
}

static esp_err_t set_motor_input(void *ctx, uint8_t input_index,
                                 uint16_t duty_permille)
{
    hotplug_motor_io_t *io = (hotplug_motor_io_t *)ctx;
    const motor_input_binding_t *input = find_input(io, input_index);
    if (NULL == io || NULL == input || !io->pwm_ready || 1000U < duty_permille) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t duty = ((uint32_t)duty_permille *
                     BOARD_LAIWFS300_MOTOR_PWM_DUTY_MAX) / 1000U;
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, input->channel, duty);
    if (ESP_OK != ret) {
        return ret;
    }
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, input->channel);
}

static esp_err_t release_pwm(void)
{
    esp_err_t first_error = ESP_OK;

    for (size_t i = 0U; i < s_motor_io.configured_channel_count; i++) {
        esp_err_t ret = ledc_stop(LEDC_LOW_SPEED_MODE,
                                  s_motor_io.inputs[i].channel, 0U);
        if (ESP_OK == first_error && ESP_OK != ret) {
            first_error = ret;
        }
    }
    if (s_motor_io.timer_configured) {
        esp_err_t ret = ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        if (ESP_OK == first_error && ESP_OK != ret) {
            first_error = ret;
        }
    }
    for (size_t i = 0U; i < HOTPLUG_MOTOR_INPUT_COUNT; i++) {
        esp_err_t ret = gpio_reset_pin(s_motor_io.inputs[i].gpio_num);
        if (ESP_OK == first_error && ESP_OK != ret) {
            first_error = ret;
        }
    }

    s_motor_io.pwm_ready = false;
    s_motor_io.timer_configured = false;
    s_motor_io.configured_channel_count = 0U;
    return first_error;
}

static esp_err_t configure_pwm(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BOARD_LAIWFS300_MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer);
    if (ESP_OK != ret) {
        return ret;
    }
    s_motor_io.timer_configured = true;

    for (size_t i = 0U; i < HOTPLUG_MOTOR_INPUT_COUNT; i++) {
        const ledc_channel_config_t channel = {
            .gpio_num = s_motor_io.inputs[i].gpio_num,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = s_motor_io.inputs[i].channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0U,
            .hpoint = 0,
        };
        ret = ledc_channel_config(&channel);
        if (ESP_OK != ret) {
            release_pwm();
            return ret;
        }
        s_motor_io.configured_channel_count++;
        ESP_LOGI(TAG, "IN%u -> GPIO%d / LEDC channel %d",
                 s_motor_io.inputs[i].input_index,
                 s_motor_io.inputs[i].gpio_num,
                 s_motor_io.inputs[i].channel);
    }
    s_motor_io.pwm_ready = true;
    return ESP_OK;
}

esp_err_t hotplug_motor_runtime_init(void)
{
    if (s_runtime_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = configure_pwm();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "PWM init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const pt2466_motor_config_t left_config = {
        .name = "left_track",
        .positive_input = 1U,
        .negative_input = 2U,
        .invert_direction = true,
        .set_input = set_motor_input,
        .user_ctx = &s_motor_io,
    };
    const pt2466_motor_config_t right_config = {
        .name = "right_track",
        .positive_input = 3U,
        .negative_input = 4U,
        .invert_direction = true,
        .set_input = set_motor_input,
        .user_ctx = &s_motor_io,
    };

    ret = pt2466_motor_init(&s_left_motor, &left_config);
    if (ESP_OK == ret) {
        ret = pt2466_motor_init(&s_right_motor, &right_config);
    }
    if (ESP_OK == ret) {
        ret = robot_motion_init(&s_motion, &s_left_motor, &s_right_motor);
    }
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "motion init failed: %s", esp_err_to_name(ret));
        release_pwm();
        memset(&s_left_motor, 0, sizeof(s_left_motor));
        memset(&s_right_motor, 0, sizeof(s_right_motor));
        memset(&s_motion, 0, sizeof(s_motion));
        return ret;
    }

    s_runtime_initialized = true;
    ESP_LOGI(TAG, "motor runtime initialized");
    return ESP_OK;
}

esp_err_t hotplug_motor_runtime_forward_100(void)
{
    if (!s_runtime_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = robot_motion_forward(&s_motion,
                                         HOTPLUG_MOTOR_FORWARD_PERCENT);
    if (ESP_OK == ret) {
        ESP_LOGI(TAG, "motor forward 100%%, continuous until D0 removal");
    }
    return ret;
}

esp_err_t hotplug_motor_runtime_deinit(void)
{
    esp_err_t first_error = ESP_OK;

    if (s_runtime_initialized) {
        esp_err_t ret = robot_motion_stop(&s_motion);
        if (ESP_OK != ret) {
            ESP_LOGE(TAG, "motor stop failed: %s", esp_err_to_name(ret));
            first_error = ret;
        } else {
            ESP_LOGI(TAG, "motor stopped");
        }
    }

    esp_err_t release_ret = release_pwm();
    if (ESP_OK == first_error && ESP_OK != release_ret) {
        first_error = release_ret;
    }

    memset(&s_left_motor, 0, sizeof(s_left_motor));
    memset(&s_right_motor, 0, sizeof(s_right_motor));
    memset(&s_motion, 0, sizeof(s_motion));
    s_runtime_initialized = false;
    ESP_LOGI(TAG, "motor runtime deinitialized, GPIOs released");
    return first_error;
}
