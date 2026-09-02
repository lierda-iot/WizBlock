#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "soc/gpio_num.h"

typedef enum {
    CAMERA_HAL_SENSOR_MODE_SP0A39_VYUY_640X480 = 0,
    CAMERA_HAL_SENSOR_MODE_SP0A39_GRAY8_200X200,
    CAMERA_HAL_SENSOR_MODE_SP0A39_GRAY8_640X480,
} camera_hal_sensor_mode_t;

typedef struct {
    gpio_num_t data_io[8];
    gpio_num_t pclk_io;
    gpio_num_t vsync_io;
    gpio_num_t hsync_io;
    uint8_t i2c_addr_7bit;
    uint8_t ioex_reset_port;
    uint8_t ioex_reset_pin;
    uint8_t ioex_pwdn_port;
    uint8_t ioex_pwdn_pin;
    uint32_t h_res;
    uint32_t v_res;
    uint32_t fb_count;
    camera_hal_sensor_mode_t sensor_mode;
} camera_hal_config_t;

esp_err_t camera_hal_init(const camera_hal_config_t *config);
esp_err_t camera_hal_capture(uint8_t **out_data, size_t *out_len, uint32_t timeout_ms);
void camera_hal_release_frame(uint8_t *data);
esp_err_t camera_hal_deinit(void);
esp_err_t camera_hal_log_sensor_output_regs(void);
esp_err_t camera_hal_set_test_pattern(bool enabled);
esp_err_t camera_hal_verify_expected_output_p0_31(const char *stage);

esp_err_t camera_hal_yuv422_crop_to_rgb565(
    const uint8_t *src_yuv422, uint32_t src_w, uint32_t src_h,
    uint16_t *dst_rgb565, uint32_t dst_w, uint32_t dst_h);
