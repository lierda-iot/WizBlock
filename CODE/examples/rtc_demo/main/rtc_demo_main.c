#include "board_laiwfs300.h"
#include "aip8563_rtc.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rtc_demo";

void app_main(void)
{
    ESP_LOGI(TAG, "RTC demo starting (AIP8563)");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = aip8563_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "AIP8563 init failed: %s", esp_err_to_name(ret));
        return;
    }

    if (aip8563_power_lost()) {
        ESP_LOGW(TAG, "RTC power was lost (VL flag set), setting time to 2026-06-26 12:00:00");
        aip8563_time_t t = {
            .year = 26, .month = 6, .day = 26,
            .hours = 12, .minutes = 0, .seconds = 0,
        };
        aip8563_set_time(&t);
    }

    ESP_LOGI(TAG, "reading RTC time every 1 second...");

    while (true) {
        aip8563_time_t t = {0};
        aip8563_get_time(&t);
        ESP_LOGI(TAG, "RTC: 20%02u-%02u-%02u %02u:%02u:%02u",
                 t.year, t.month, t.day, t.hours, t.minutes, t.seconds);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
