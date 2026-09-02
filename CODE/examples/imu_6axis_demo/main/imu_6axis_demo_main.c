#include "bmi260_imu.h"
#include "board_laiwfs300.h"
#include "imu_6axis_demo_logic.h"
#include "imu_6axis_demo_ui.h"
#include "launcher_return.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "imu_6axis_demo";

#define SENSOR_SAMPLE_INTERVAL_MS       50U
#define UI_REFRESH_INTERVAL_MS          50U
#define SENSOR_TASK_STACK                4096U
#define UI_REFRESH_TASK_STACK            4096U
#define SENSOR_TASK_PRIORITY             (tskIDLE_PRIORITY + 3U)
#define UI_REFRESH_TASK_PRIORITY         (tskIDLE_PRIORITY + 2U)
#define STATUS_LOCK_TIMEOUT_MS           20U
#define CALIBRATION_SAMPLE_COUNT        20U
#define LOG_INTERVAL_MS                 200U

static SemaphoreHandle_t s_status_mutex;
static TaskHandle_t s_sensor_task_handle;
static imu_demo_status_t s_status = {
    .sensor_ok = true,
    .calibrating = true,
    .calibration_percent = 0U,
};
static bool s_calibration_requested = true;

static bool status_lock(void)
{
    if (NULL == s_status_mutex) {
        return false;
    }
    return pdTRUE == xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(STATUS_LOCK_TIMEOUT_MS));
}

static void status_unlock(void)
{
    if (NULL != s_status_mutex) {
        xSemaphoreGive(s_status_mutex);
    }
}

static void publish_status(const imu_demo_status_t *status)
{
    if (NULL == status) {
        return;
    }
    if (status_lock()) {
        s_status = *status;
        status_unlock();
    }
}

static bool copy_status(imu_demo_status_t *status)
{
    bool copied = false;

    if (NULL == status) {
        return false;
    }
    if (status_lock()) {
        *status = s_status;
        copied = true;
        status_unlock();
    }
    return copied;
}

static bool take_calibration_request(void)
{
    bool requested = false;

    if (status_lock()) {
        requested = s_calibration_requested;
        s_calibration_requested = false;
        status_unlock();
    }
    return requested;
}

static void request_calibration(void *user_ctx)
{
    (void)user_ctx;

    if (status_lock()) {
        s_calibration_requested = true;
        status_unlock();
    }
}

static void begin_calibration(imu_demo_filter_t *filter,
                              int64_t *gyro_sum_x,
                              int64_t *gyro_sum_y,
                              int64_t *gyro_sum_z,
                              uint32_t *sample_count)
{
    if (NULL == filter || NULL == gyro_sum_x || NULL == gyro_sum_y ||
        NULL == gyro_sum_z || NULL == sample_count) {
        return;
    }

    imu_demo_filter_init(filter);
    *gyro_sum_x = 0;
    *gyro_sum_y = 0;
    *gyro_sum_z = 0;
    *sample_count = 0U;
}

static void set_error_status(const char *error_text,
                             const imu_demo_raw_vector_t *accel,
                             const imu_demo_raw_vector_t *gyro)
{
    imu_demo_status_t status = {0};

    if (NULL == error_text || NULL == accel || NULL == gyro) {
        return;
    }
    status.sensor_ok = false;
    status.calibrating = false;
    status.accel_raw = *accel;
    status.gyro_raw = *gyro;
    snprintf(status.error_text, sizeof(status.error_text), "%s", error_text);
    publish_status(&status);
}

static void sensor_task(void *arg)
{
    imu_demo_filter_t filter = {0};
    int64_t gyro_sum_x = 0;
    int64_t gyro_sum_y = 0;
    int64_t gyro_sum_z = 0;
    uint32_t calibration_count = 0U;
    bool calibrating = true;
    int64_t last_sample_us = esp_timer_get_time();
    int64_t last_log_us = last_sample_us;
    TickType_t last_wake = xTaskGetTickCount();

    (void)arg;
    begin_calibration(&filter, &gyro_sum_x, &gyro_sum_y, &gyro_sum_z, &calibration_count);

    while (true) {
        bmi260_raw_data_t accel_raw = {0};
        bmi260_raw_data_t gyro_raw = {0};
        imu_demo_raw_vector_t accel = {0};
        imu_demo_raw_vector_t gyro = {0};
        imu_demo_sample_t sample = {0};
        imu_demo_status_t status = {0};
        int64_t now_us = esp_timer_get_time();
        float dt_seconds = (float)(now_us - last_sample_us) / 1000000.0f;
        if (take_calibration_request()) {
            calibrating = true;
            begin_calibration(&filter, &gyro_sum_x, &gyro_sum_y,
                              &gyro_sum_z, &calibration_count);
        }

        if (ESP_OK != bmi260_read_accel(&accel_raw) ||
            ESP_OK != bmi260_read_gyro(&gyro_raw)) {
            set_error_status("BMI260 read failed", &accel, &gyro);
            ESP_LOGW(TAG, "BMI260 read failed");
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS));
            last_sample_us = esp_timer_get_time();
            continue;
        }

        accel = (imu_demo_raw_vector_t){accel_raw.x, accel_raw.y, accel_raw.z};
        gyro = (imu_demo_raw_vector_t){gyro_raw.x, gyro_raw.y, gyro_raw.z};
        imu_demo_filter_update(&filter, accel, gyro, dt_seconds, &sample);

        status.accel_raw = accel;
        status.gyro_raw = gyro;
        status.sample = sample;
        status.sensor_ok = true;
        status.calibrating = calibrating;

        if (calibrating) {
            gyro_sum_x += gyro.x;
            gyro_sum_y += gyro.y;
            gyro_sum_z += gyro.z;
            calibration_count++;
            status.calibration_percent = (uint8_t)((calibration_count * 100U) /
                                                    CALIBRATION_SAMPLE_COUNT);

            if (calibration_count >= CALIBRATION_SAMPLE_COUNT) {
                const imu_demo_raw_vector_t bias = {
                    .x = (int16_t)(gyro_sum_x / CALIBRATION_SAMPLE_COUNT),
                    .y = (int16_t)(gyro_sum_y / CALIBRATION_SAMPLE_COUNT),
                    .z = (int16_t)(gyro_sum_z / CALIBRATION_SAMPLE_COUNT),
                };

                imu_demo_filter_set_gyro_bias(&filter, bias);
                imu_demo_filter_reset_attitude(&filter, &accel);
                imu_demo_filter_update(&filter, accel, gyro, dt_seconds, &status.sample);
                status.calibrating = false;
                status.calibration_percent = 100U;
                calibrating = false;
                ESP_LOGI(TAG, "gyro calibration complete: bias=[%d %d %d]",
                         bias.x, bias.y, bias.z);
            }
        }

        publish_status(&status);

        if ((now_us - last_log_us) >= (LOG_INTERVAL_MS * 1000LL)) {
            ESP_LOGI(TAG,
                     "accel[%d %d %d] gyro[%d %d %d] roll=%+.1f pitch=%+.1f yaw=%+.1f motion=%s",
                     accel.x, accel.y, accel.z,
                     gyro.x, gyro.y, gyro.z,
                     (double)status.sample.roll_deg,
                     (double)status.sample.pitch_deg,
                     (double)status.sample.yaw_deg,
                     imu_demo_motion_text(status.sample.motion));
            last_log_us = now_us;
        }

        last_sample_us = now_us;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS));
    }
}

static void ui_refresh_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)arg;

    while (true) {
        imu_demo_status_t status = {0};

        if (copy_status(&status)) {
            (void)imu_demo_ui_update(&status);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UI_REFRESH_INTERVAL_MS));
    }
}

void app_main(void)
{
    const imu_demo_ui_callbacks_t callbacks = {
        .on_calibrate = request_calibration,
        .user_ctx = NULL,
    };
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "BMI260 6-axis motion lab starting");

    ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = launcher_return_start_default();
    if (ESP_OK != ret && ESP_ERR_NOT_SUPPORTED != ret) {
        ESP_LOGW(TAG, "launcher return unavailable: %s", esp_err_to_name(ret));
    }

    ret = bmi260_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "BMI260 init failed: %s", esp_err_to_name(ret));
        return;
    }

    s_status_mutex = xSemaphoreCreateMutex();
    if (NULL == s_status_mutex) {
        ESP_LOGE(TAG, "status mutex create failed");
        return;
    }

    ret = imu_demo_ui_init(&callbacks);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "IMU UI init failed: %s", esp_err_to_name(ret));
        return;
    }

    task_ok = xTaskCreate(sensor_task, "imu_sensor", SENSOR_TASK_STACK, NULL,
                          SENSOR_TASK_PRIORITY, &s_sensor_task_handle);
    if (pdPASS != task_ok) {
        ESP_LOGE(TAG, "sensor task create failed");
        return;
    }

    task_ok = xTaskCreate(ui_refresh_task, "imu_ui_sync", UI_REFRESH_TASK_STACK, NULL,
                          UI_REFRESH_TASK_PRIORITY, NULL);
    if (pdPASS != task_ok) {
        ESP_LOGE(TAG, "UI refresh task create failed");
        return;
    }

    ESP_LOGI(TAG, "BMI260 motion lab ready: calibrating for 1s");
}
