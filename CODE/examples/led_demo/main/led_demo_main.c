#include "board_laiwfs300.h"
#include "board_pins.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "led_demo";

#define LED_GPIO_NUM       BOARD_LAIWFS300_GPIO_RGB_LED
#define LED_COUNT          1
#define STATE_INTERVAL_MS  500

typedef struct {
    const char *name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_state_t;

static const led_state_t s_states[] = {
    { "off", 0, 0, 0 },
    { "red", 64, 0, 0 },
    { "green", 0, 64, 0 },
    { "blue", 0, 0, 64 },
    { "yellow", 64, 64, 0 },
    { "cyan", 0, 64, 64 },
    { "magenta", 64, 0, 64 },
    { "white", 64, 64, 64 },
};

static esp_err_t led_apply_state(led_strip_handle_t strip, const led_state_t *state)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, state->red, state->green, state->blue));
    return led_strip_refresh(strip);
}

void app_main(void)
{
    ESP_LOGI(TAG, "LED demo starting (GPIO%d / RGB_PWM)", LED_GPIO_NUM);

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    led_strip_handle_t strip = NULL;
    const led_strip_config_t led_config = {
        .strip_gpio_num = LED_GPIO_NUM,
        .max_leds = LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = false },
    };
    const led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10000000,
        .flags = { .with_dma = false },
    };

    ret = led_strip_new_rmt_device(&led_config, &rmt_config, &strip);
    if (ESP_OK != ret || NULL == strip) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "cycling RGB states every %d ms", STATE_INTERVAL_MS);

    size_t idx = 0;
    while (true) {
        const led_state_t *state = &s_states[idx];
        ESP_LOGI(TAG, "state=%s rgb=(%u,%u,%u)",
                 state->name, state->red, state->green, state->blue);
        ret = led_apply_state(strip, state);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "refresh failed: %s", esp_err_to_name(ret));
        }
        idx = (idx + 1) % (sizeof(s_states) / sizeof(s_states[0]));
        vTaskDelay(pdMS_TO_TICKS(STATE_INTERVAL_MS));
    }
}
