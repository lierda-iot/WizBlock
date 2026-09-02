#include "lte_hal.h"
#include "io_expander.h"
#include "board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lte_hal";
static lte_state_t s_state = LTE_STATE_OFF;

#define LTE_POWER_ON_WAIT_MS 3000

esp_err_t lte_hal_init(void)
{
    esp_err_t ret = io_expander_set_pin_direction(
        BOARD_LAIWFS300_IOEX_LTE_PWR_PORT,
        BOARD_LAIWFS300_IOEX_LTE_PWR_PIN,
        true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set LTE_PWR pin direction: %s", esp_err_to_name(ret));
        s_state = LTE_STATE_ERROR;
        return ret;
    }

    ret = io_expander_write_pin(
        BOARD_LAIWFS300_IOEX_LTE_PWR_PORT,
        BOARD_LAIWFS300_IOEX_LTE_PWR_PIN,
        false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set LTE_PWR low: %s", esp_err_to_name(ret));
        s_state = LTE_STATE_ERROR;
        return ret;
    }

    s_state = LTE_STATE_OFF;
    ESP_LOGI(TAG, "init ok, power off");
    return ESP_OK;
}

esp_err_t lte_hal_power_on(void)
{
    if (s_state == LTE_STATE_READY) {
        return ESP_OK;
    }

    s_state = LTE_STATE_POWERING_ON;
    ESP_LOGI(TAG, "powering on LTE module (IOEX P1_1 HIGH)");

    esp_err_t ret = io_expander_write_pin(
        BOARD_LAIWFS300_IOEX_LTE_PWR_PORT,
        BOARD_LAIWFS300_IOEX_LTE_PWR_PIN,
        true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to enable LTE_PWR: %s", esp_err_to_name(ret));
        s_state = LTE_STATE_ERROR;
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(LTE_POWER_ON_WAIT_MS));

    s_state = LTE_STATE_READY;
    ESP_LOGI(TAG, "LTE module powered on, ready for USB ECM enumeration");
    return ESP_OK;
}

esp_err_t lte_hal_power_off(void)
{
    ESP_LOGI(TAG, "powering off LTE module (IOEX P1_1 LOW)");

    esp_err_t ret = io_expander_write_pin(
        BOARD_LAIWFS300_IOEX_LTE_PWR_PORT,
        BOARD_LAIWFS300_IOEX_LTE_PWR_PIN,
        false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to disable LTE_PWR: %s", esp_err_to_name(ret));
        s_state = LTE_STATE_ERROR;
        return ret;
    }

    s_state = LTE_STATE_OFF;
    return ESP_OK;
}

lte_state_t lte_hal_get_state(void)
{
    return s_state;
}
