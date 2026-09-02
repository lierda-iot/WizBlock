#include "companion_input.h"

#include "board_adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMPANION_SW3_ADC_CHANNEL ADC_CHANNEL_7
#define COMPANION_INPUT_TASK_STACK 3072U
#define COMPANION_INPUT_TASK_PRIORITY 3U
#define COMPANION_INPUT_LOG_MS 10000U
#define COMPANION_INPUT_ERROR_LIMIT 3U
#define COMPANION_INPUT_RECOVERY_LIMIT 3U

typedef enum {
    SW3_SAMPLE_UNKNOWN = 0,
    SW3_SAMPLE_RELEASED,
    SW3_SAMPLE_PRESSED,
} sw3_sample_state_t;

static const char *TAG = "companion_input";
static companion_input_config_t s_config;
static bool s_started;
static bool s_starting;
static portMUX_TYPE s_lifecycle_lock = portMUX_INITIALIZER_UNLOCKED;

static sw3_sample_state_t classify_sample(int raw)
{
    if (raw <= s_config.pressed_raw_max) {
        return SW3_SAMPLE_PRESSED;
    }
    if (raw >= s_config.released_raw_min) {
        return SW3_SAMPLE_RELEASED;
    }
    return SW3_SAMPLE_UNKNOWN;
}

static void input_task(void *arg)
{
    (void)arg;
    adc_oneshot_unit_handle_t adc = board_adc_handle();
    sw3_sample_state_t stable = SW3_SAMPLE_UNKNOWN;
    sw3_sample_state_t candidate = SW3_SAMPLE_UNKNOWN;
    uint32_t candidate_count = 0U;
    uint32_t press_started_ms = 0U;
    uint32_t long_press_started_ms = 0U;
    uint32_t consecutive_errors = 0U;
    uint32_t consecutive_successes = 0U;
    bool health_available = true;
    bool click_armed = false;
    bool long_press_armed = false;
    bool long_press_active = false;
    bool long_press_triggered = false;
    int raw_min = INT32_MAX;
    int raw_max = INT32_MIN;
    uint32_t last_log_ms = 0U;
    const uint32_t required_samples =
        (s_config.debounce_ms + s_config.sample_ms - 1U) / s_config.sample_ms;

    ESP_LOGI(TAG,
             "SW3 task ready channel=ADC1_CH7 pressed<=%d released>=%d sample=%lums debounce=%lums max_click=%lums long=%lums",
             s_config.pressed_raw_max, s_config.released_raw_min,
             (unsigned long)s_config.sample_ms,
             (unsigned long)s_config.debounce_ms,
             (unsigned long)s_config.max_click_ms,
             (unsigned long)s_config.long_press_ms);

    while (true) {
        int raw = 0;
        esp_err_t result = adc_oneshot_read(adc, COMPANION_SW3_ADC_CHANNEL, &raw);
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (ESP_OK != result) {
            consecutive_errors++;
            consecutive_successes = 0U;
            if (health_available &&
                COMPANION_INPUT_ERROR_LIMIT <= consecutive_errors) {
                health_available = false;
                stable = SW3_SAMPLE_UNKNOWN;
                candidate = SW3_SAMPLE_UNKNOWN;
                candidate_count = 0U;
                press_started_ms = 0U;
                long_press_started_ms = 0U;
                click_armed = false;
                long_press_armed = false;
                long_press_active = false;
                long_press_triggered = false;
                ESP_LOGE(TAG, "SW3 ADC unavailable error=%s", esp_err_to_name(result));
                if (NULL != s_config.on_error) {
                    s_config.on_error(result, s_config.user_ctx);
                }
                if (NULL != s_config.on_health) {
                    s_config.on_health(false, result, s_config.user_ctx);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(s_config.sample_ms));
            continue;
        }

        consecutive_errors = 0U;
        if (!health_available) {
            consecutive_successes++;
            if (COMPANION_INPUT_RECOVERY_LIMIT <= consecutive_successes) {
                health_available = true;
                consecutive_successes = 0U;
                ESP_LOGI(TAG, "SW3 ADC recovered after %u reads raw=%d",
                         COMPANION_INPUT_RECOVERY_LIMIT, raw);
                if (NULL != s_config.on_health) {
                    s_config.on_health(true, ESP_OK, s_config.user_ctx);
                }
            }
        } else {
            consecutive_successes = 0U;
        }
        if (!health_available) {
            vTaskDelay(pdMS_TO_TICKS(s_config.sample_ms));
            continue;
        }
        if (raw < raw_min) {
            raw_min = raw;
        }
        if (raw > raw_max) {
            raw_max = raw;
        }

        const sw3_sample_state_t sample = classify_sample(raw);
        if (SW3_SAMPLE_UNKNOWN == sample) {
            candidate = SW3_SAMPLE_UNKNOWN;
            candidate_count = 0U;
        } else if (sample != candidate) {
            candidate = sample;
            candidate_count = 1U;
        } else if (required_samples > candidate_count) {
            candidate_count++;
        }

        if (sample != SW3_SAMPLE_UNKNOWN && candidate_count >= required_samples &&
            stable != candidate) {
            stable = candidate;
            if (SW3_SAMPLE_PRESSED == stable) {
                press_started_ms = now_ms;
                click_armed = true;
                long_press_started_ms = now_ms;
                long_press_active = long_press_armed;
                long_press_triggered = false;
                ESP_LOGI(TAG, "SW3 pressed raw=%d", raw);
            } else {
                const uint32_t held_ms = now_ms - press_started_ms;
                ESP_LOGI(TAG, "SW3 released raw=%d held=%lums armed=%u",
                         raw, (unsigned long)held_ms, click_armed ? 1U : 0U);
                if (click_armed && held_ms <= s_config.max_click_ms &&
                    NULL != s_config.on_click) {
                    s_config.on_click(s_config.user_ctx);
                }
                click_armed = false;
                long_press_armed = true;
                long_press_active = false;
                long_press_triggered = false;
            }
        }

        if (long_press_active && !long_press_triggered &&
            NULL != s_config.on_long_press && 0U != s_config.long_press_ms &&
            SW3_SAMPLE_PRESSED == sample &&
            (now_ms - long_press_started_ms) >= s_config.long_press_ms) {
            long_press_triggered = true;
            click_armed = false;
            ESP_LOGI(TAG, "SW3 long press held=%lums",
                     (unsigned long)(now_ms - long_press_started_ms));
            s_config.on_long_press(s_config.user_ctx);
        }

        if ((now_ms - last_log_ms) >= COMPANION_INPUT_LOG_MS) {
            last_log_ms = now_ms;
            ESP_LOGI(TAG, "SW3 ADC raw=%d range=[%d,%d] stable=%d",
                     raw, raw_min, raw_max, (int)stable);
            raw_min = raw;
            raw_max = raw;
        }
        vTaskDelay(pdMS_TO_TICKS(s_config.sample_ms));
    }
}

esp_err_t companion_input_start(const companion_input_config_t *config)
{
    if (NULL == config || NULL == config->on_click ||
        (NULL == config->on_error && NULL == config->on_health) ||
        (0U != config->long_press_ms && NULL == config->on_long_press) ||
        0 > config->pressed_raw_max ||
        config->pressed_raw_max >= config->released_raw_min ||
        0U == config->sample_ms || 0U == config->debounce_ms ||
        0U == config->max_click_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    if (NULL == board_adc_handle()) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (s_started) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_OK;
    }
    if (s_starting) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_starting = true;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    s_config = *config;
    BaseType_t result = xTaskCreatePinnedToCore(
        input_task, "companion_input", COMPANION_INPUT_TASK_STACK, NULL,
        COMPANION_INPUT_TASK_PRIORITY, NULL, 1);
    if (pdPASS == result) {
        portENTER_CRITICAL(&s_lifecycle_lock);
        s_starting = false;
        s_started = true;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_starting = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return ESP_FAIL;
}
