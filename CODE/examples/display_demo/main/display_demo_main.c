#include "board_laiwfs300.h"
#include "display_hal.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display_demo";

void app_main(void)
{
    ESP_LOGI(TAG, "Display demo starting (ST7789V3 240x320)");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = board_laiwfs300_display_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "display initialized, cycling colors...");

    const uint16_t colors[] = {
        DISPLAY_HAL_RGB565_WHITE,
        DISPLAY_HAL_RGB565_RED,
        0x07E0,
        0x001F,
    };
    const char *names[] = {"white", "red", "green", "blue"};
    const size_t num_colors = sizeof(colors) / sizeof(colors[0]);
    size_t idx = 0;

    while (true) {
        ESP_LOGI(TAG, "fill: %s", names[idx]);
        board_laiwfs300_display_fill_rgb565(colors[idx]);
        idx = (idx + 1) % num_colors;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
