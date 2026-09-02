#include "board_laiwfs300.h"

#include "board_pins.h"
#include "camera_hal.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board_camera";
static bool s_camera_initialized;
static camera_hal_sensor_mode_t s_camera_mode;

static esp_err_t board_laiwfs300_camera_init_with_mode(
    camera_hal_sensor_mode_t sensor_mode, uint32_t h_res, uint32_t v_res)
{
    if (s_camera_initialized) {
        ESP_RETURN_ON_FALSE(s_camera_mode == sensor_mode, ESP_ERR_INVALID_STATE,
                            TAG, "camera already initialized in mode %d",
                            (int)s_camera_mode);
        return ESP_OK;
    }

    const camera_hal_config_t config = {
        .data_io = {
            BOARD_LAIWFS300_GPIO_CAMERA_D0,
            BOARD_LAIWFS300_GPIO_CAMERA_D1,
            BOARD_LAIWFS300_GPIO_CAMERA_D2,
            BOARD_LAIWFS300_GPIO_CAMERA_D3,
            BOARD_LAIWFS300_GPIO_CAMERA_D4,
            BOARD_LAIWFS300_GPIO_CAMERA_D5,
            BOARD_LAIWFS300_GPIO_CAMERA_D6,
            BOARD_LAIWFS300_GPIO_CAMERA_D7,
        },
        .pclk_io = BOARD_LAIWFS300_GPIO_CAMERA_PCLK,
        .vsync_io = BOARD_LAIWFS300_GPIO_CAMERA_VSYNC,
        .hsync_io = BOARD_LAIWFS300_GPIO_CAMERA_HSYNC,
        .i2c_addr_7bit = BOARD_LAIWFS300_SP0A39_I2C_ADDR_7BIT,
        .ioex_reset_port = BOARD_LAIWFS300_IOEX_CAMERA_RESET_PORT,
        .ioex_reset_pin = BOARD_LAIWFS300_IOEX_CAMERA_RESET_PIN,
        .ioex_pwdn_port = BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PORT,
        .ioex_pwdn_pin = BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PIN,
        .h_res = h_res,
        .v_res = v_res,
        .fb_count = 2,
        .sensor_mode = sensor_mode,
    };

    ESP_LOGI(TAG, "initializing SP0A39 camera mode=%d %lux%lu",
             (int)sensor_mode, (unsigned long)h_res, (unsigned long)v_res);
    ESP_RETURN_ON_ERROR(camera_hal_init(&config), TAG, "camera_hal_init");

    s_camera_mode = sensor_mode;
    s_camera_initialized = true;
    ESP_LOGI(TAG, "camera ready");
    return ESP_OK;
}

esp_err_t board_laiwfs300_camera_init(void)
{
    return board_laiwfs300_camera_init_with_mode(
        CAMERA_HAL_SENSOR_MODE_SP0A39_VYUY_640X480, 640U, 480U);
}

esp_err_t board_laiwfs300_camera_init_gray_200x200(void)
{
    return board_laiwfs300_camera_init_with_mode(
        CAMERA_HAL_SENSOR_MODE_SP0A39_GRAY8_200X200, 200U, 200U);
}

esp_err_t board_laiwfs300_camera_init_gray_640x480(void)
{
    return board_laiwfs300_camera_init_with_mode(
        CAMERA_HAL_SENSOR_MODE_SP0A39_GRAY8_640X480, 640U, 480U);
}

esp_err_t board_laiwfs300_camera_capture(uint8_t **out_data, size_t *out_len)
{
    ESP_RETURN_ON_FALSE(s_camera_initialized, ESP_ERR_INVALID_STATE, TAG, "camera not init");
    return camera_hal_capture(out_data, out_len, 5000);
}

void board_laiwfs300_camera_release_frame(uint8_t *data)
{
    camera_hal_release_frame(data);
}
