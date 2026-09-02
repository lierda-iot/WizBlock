#include "camera_hal.h"

#include "io_expander.h"
#include "bus_i2c.h"
#include "sp0a39_regs.h"
#include "sp0a39_gray_200x200_regs.h"
#include "sp0a39_gray_640x480_regs.h"
#include "camera_hal_policy.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "camera_hal";

#define SP0A39_PAGE_SELECT_REG 0xFDU
#define SP0A39_PAGE_0 0x00U
#define SP0A39_PAGE_1 0x01U
#define SP0A39_TEST_PATTERN_REG 0x32U
#define SP0A39_TEST_PATTERN_MASK 0x80U

static camera_hal_config_t s_config;
static i2c_master_dev_handle_t s_sensor_dev;
static bool s_initialized;

static esp_err_t sensor_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_sensor_dev, buf, 2, 50);
}

static esp_err_t sensor_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_sensor_dev, &reg, 1, val, 1, 50);
}

static esp_err_t sensor_write_all_regs(void)
{
    const uint8_t (*regs)[2] = NULL;
    size_t count = 0U;
    const char *mode_name = NULL;

    if (CAMERA_HAL_SENSOR_MODE_SP0A39_VYUY_640X480 == s_config.sensor_mode) {
        regs = s_sp0a39_regs;
        count = sizeof(s_sp0a39_regs) / sizeof(s_sp0a39_regs[0]);
        mode_name = "VYUY 640x480";
    } else if (CAMERA_HAL_SENSOR_MODE_SP0A39_GRAY8_200X200 == s_config.sensor_mode) {
        regs = s_sp0a39_gray_200x200_regs;
        count = sizeof(s_sp0a39_gray_200x200_regs) /
                sizeof(s_sp0a39_gray_200x200_regs[0]);
        mode_name = "Gray8 200x200";
    } else if (CAMERA_HAL_SENSOR_MODE_SP0A39_GRAY8_640X480 == s_config.sensor_mode) {
        regs = s_sp0a39_gray_640x480_regs;
        count = sizeof(s_sp0a39_gray_640x480_regs) /
                sizeof(s_sp0a39_gray_640x480_regs[0]);
        mode_name = "Gray8 640x480";
    } else {
        ESP_LOGE(TAG, "unsupported SP0A39 sensor mode: %d", (int)s_config.sensor_mode);
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < count; i++) {
        ESP_RETURN_ON_ERROR(sensor_write_reg(regs[i][0], regs[i][1]),
                            TAG, "reg[%u] 0x%02x=0x%02x", (unsigned)i,
                            regs[i][0], regs[i][1]);
    }
    ESP_LOGI(TAG, "SP0A39 register init done: %s (%u regs)",
             mode_name, (unsigned)count);
    return ESP_OK;
}

esp_err_t camera_hal_verify_expected_output_p0_31(const char *stage)
{
    ESP_RETURN_ON_FALSE(s_initialized || NULL != s_sensor_dev,
                        ESP_ERR_INVALID_STATE, TAG, "sensor not initialized");
    if (NULL == stage) {
        stage = "unspecified";
    }

    ESP_RETURN_ON_ERROR(sensor_write_reg(0xfd, 0x00), TAG, "page0 select for p0_31 probe");
    uint8_t value = 0U;
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x31, &value), TAG, "read p0_31 probe");
    ESP_LOGI(TAG, "[CAM-PROBE] SP0A39 p0_31 stage=%s value=0x%02x expected=0x%02x",
             stage, value, CAMERA_HAL_SP0A39_EXPECTED_P0_31);
    ESP_RETURN_ON_FALSE(camera_hal_sensor_output_p0_31_valid(value),
                        ESP_ERR_INVALID_RESPONSE, TAG,
                        "unexpected p0_31=0x%02x at %s", value, stage);
    return ESP_OK;
}

static esp_err_t sensor_read_id(void)
{
    ESP_RETURN_ON_ERROR(sensor_write_reg(0xfd, 0x00), TAG, "page select");
    uint8_t id_h = 0, id_l = 0;
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x00, &id_h), TAG, "read id_h");
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x01, &id_l), TAG, "read id_l");
    ESP_LOGI(TAG, "SP0A39 chip ID: 0x%02X%02X", id_h, id_l);
    ESP_RETURN_ON_FALSE(camera_hal_sensor_id_valid(id_h, id_l), ESP_ERR_INVALID_RESPONSE,
                        TAG, "unexpected SP0A39 chip ID 0x%02X%02X", id_h, id_l);
    return ESP_OK;
}

esp_err_t camera_hal_log_sensor_output_regs(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    uint8_t p0_1c = 0;
    uint8_t p0_30 = 0;
    uint8_t p0_31 = 0;
    uint8_t p1_32 = 0;
    uint8_t p1_34 = 0;
    uint8_t p1_35 = 0;
    uint8_t p1_36 = 0;

    ESP_RETURN_ON_ERROR(sensor_write_reg(0xfd, 0x00), TAG, "page0 select");
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x1c, &p0_1c), TAG, "read p0_1c");
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x30, &p0_30), TAG, "read p0_30");
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x31, &p0_31), TAG, "read p0_31");

    ESP_RETURN_ON_ERROR(sensor_write_reg(0xfd, 0x01), TAG, "page1 select");
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x32, &p1_32), TAG, "read p1_32");
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x34, &p1_34), TAG, "read p1_34");
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x35, &p1_35), TAG, "read p1_35");
    ESP_RETURN_ON_ERROR(sensor_read_reg(0x36, &p1_36), TAG, "read p1_36");

    ESP_RETURN_ON_ERROR(sensor_write_reg(0xfd, 0x00), TAG, "restore page0");
    ESP_LOGI(TAG,
             "SP0A39 output regs: P0:1c=0x%02x P0:30=0x%02x P0:31=0x%02x "
             "P1:32=0x%02x P1:34=0x%02x P1:35=0x%02x P1:36=0x%02x",
             p0_1c, p0_30, p0_31, p1_32, p1_34, p1_35, p1_36);
    return ESP_OK;
}

esp_err_t camera_hal_set_test_pattern(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    uint8_t before = 0U;
    uint8_t after = 0U;
    uint8_t write_value = 0U;
    const uint8_t expected_bit = enabled ? SP0A39_TEST_PATTERN_MASK : 0U;
    esp_err_t ret = sensor_write_reg(SP0A39_PAGE_SELECT_REG, SP0A39_PAGE_1);
    if (ESP_OK == ret) {
        ret = sensor_read_reg(SP0A39_TEST_PATTERN_REG, &before);
    }
    if (ESP_OK == ret) {
        write_value = enabled
            ? (uint8_t)(before | SP0A39_TEST_PATTERN_MASK)
            : (uint8_t)(before & (uint8_t)~SP0A39_TEST_PATTERN_MASK);
        ret = sensor_write_reg(SP0A39_TEST_PATTERN_REG, write_value);
    }
    if (ESP_OK == ret) {
        ret = sensor_read_reg(SP0A39_TEST_PATTERN_REG, &after);
    }

    const esp_err_t restore_ret =
        sensor_write_reg(SP0A39_PAGE_SELECT_REG, SP0A39_PAGE_0);
    if ((ESP_OK == ret) && (ESP_OK != restore_ret)) {
        ret = restore_ret;
    }
    if (ESP_OK != ret) {
        ESP_LOGE(TAG,
                 "[CAM-DIAG-C16I] SP0A39 test_pattern=%s failed: %s; page0_restore=%s",
                 enabled ? "on" : "off", esp_err_to_name(ret),
                 esp_err_to_name(restore_ret));
        return ret;
    }

    ESP_LOGW(TAG,
             "[CAM-DIAG-C16I] SP0A39 test_pattern=%s P1:32 before=0x%02x write=0x%02x "
             "read=0x%02x expected_bit=0x%02x",
             enabled ? "on" : "off", before, write_value, after, expected_bit);
    ESP_RETURN_ON_FALSE((after & SP0A39_TEST_PATTERN_MASK) == expected_bit,
                        ESP_ERR_INVALID_RESPONSE, TAG,
                        "SP0A39 test-pattern bit readback mismatch: 0x%02x != 0x%02x",
                        after & SP0A39_TEST_PATTERN_MASK, expected_bit);
    return ESP_OK;
}

static esp_err_t sensor_power_up(void)
{
    ESP_LOGI(TAG, "configuring IOEX pins for camera RESET/PWDN");
    ESP_RETURN_ON_ERROR(io_expander_set_pin_direction(s_config.ioex_pwdn_port, s_config.ioex_pwdn_pin, true),
                        TAG, "PWDN dir");
    ESP_RETURN_ON_ERROR(io_expander_set_pin_direction(s_config.ioex_reset_port, s_config.ioex_reset_pin, true),
                        TAG, "RESET dir");

    ESP_LOGI(TAG, "PWDN=HIGH (power down)");
    ESP_RETURN_ON_ERROR(io_expander_write_pin(s_config.ioex_pwdn_port, s_config.ioex_pwdn_pin, true),
                        TAG, "PWDN high");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "PWDN=LOW (release sensor)");
    ESP_RETURN_ON_ERROR(io_expander_write_pin(s_config.ioex_pwdn_port, s_config.ioex_pwdn_pin, false),
                        TAG, "PWDN low");
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "RESET=LOW (assert reset)");
    ESP_RETURN_ON_ERROR(io_expander_write_pin(s_config.ioex_reset_port, s_config.ioex_reset_pin, false),
                        TAG, "RESET low");
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "RESET=HIGH (release reset)");
    ESP_RETURN_ON_ERROR(io_expander_write_pin(s_config.ioex_reset_port, s_config.ioex_reset_pin, true),
                        TAG, "RESET high");
    vTaskDelay(pdMS_TO_TICKS(1000));

    return ESP_OK;
}

esp_err_t camera_hal_init(const camera_hal_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(NULL != config, ESP_ERR_INVALID_ARG, TAG, "missing config");
    s_config = *config;

    ESP_LOGI(TAG, "init SP0A39 DVP %lux%lu (SCCB manual mode)",
             (unsigned long)s_config.h_res, (unsigned long)s_config.v_res);

    ESP_RETURN_ON_ERROR(sensor_power_up(), TAG, "power up");

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not ready");

    ESP_LOGI(TAG, "full I2C scan after camera power-up (diagnostic only)...");
    uint8_t acked[0x78] = {0};
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (ESP_OK == i2c_master_probe(bus, addr, 100)) {
            ESP_LOGI(TAG, "  post-powerup ACK at 0x%02X", addr);
            acked[addr] = 1;
        }
    }

    const uint8_t found_addr = camera_hal_select_sensor_addr(
        s_config.i2c_addr_7bit, acked, sizeof(acked));

    ESP_LOGI(TAG, "DVP GPIO diagnostics (checking PCLK/VSYNC/HSYNC activity)...");
    gpio_config_t diag_cfg = {
        .pin_bit_mask = (1ULL << s_config.pclk_io) | (1ULL << s_config.vsync_io) | (1ULL << s_config.hsync_io),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&diag_cfg);
    int pclk_changes = 0, vsync_changes = 0, hsync_changes = 0;
    int last_pclk = gpio_get_level(s_config.pclk_io);
    int last_vsync = gpio_get_level(s_config.vsync_io);
    int last_hsync = gpio_get_level(s_config.hsync_io);
    for (int i = 0; i < 100000; i++) {
        int p = gpio_get_level(s_config.pclk_io);
        int v = gpio_get_level(s_config.vsync_io);
        int h = gpio_get_level(s_config.hsync_io);
        if (p != last_pclk) { pclk_changes++; last_pclk = p; }
        if (v != last_vsync) { vsync_changes++; last_vsync = v; }
        if (h != last_hsync) { hsync_changes++; last_hsync = h; }
    }
    ESP_LOGI(TAG, "DVP diagnostics: PCLK_changes=%d VSYNC_changes=%d HSYNC_changes=%d",
             pclk_changes, vsync_changes, hsync_changes);
    if (0 == pclk_changes) {
        ESP_LOGW(TAG, "PCLK has no transitions - MCLK likely not reaching sensor or sensor not active");
    }

    if (0 == found_addr) {
        ESP_LOGE(TAG, "configured SP0A39 address 0x%02X did not ACK after power-up",
                 s_config.i2c_addr_7bit);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "using sensor address 0x%02X", found_addr);

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = found_addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_sensor_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add sensor I2C failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    ESP_LOGI(TAG, "SP0A39 I2C device added at 0x%02x", found_addr);

    ret = sensor_read_id();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SP0A39 identity check failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    ret = sensor_write_all_regs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SP0A39 register init failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = camera_hal_verify_expected_output_p0_31("post_table");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[CAM-PROBE] output state differs after table; continuing: %s",
                 esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    ret = camera_hal_verify_expected_output_p0_31("post_delay");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[CAM-PROBE] output state differs after delay; continuing: %s",
                 esp_err_to_name(ret));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "camera HAL initialized (sensor ready, DVP capture not yet implemented)");
    return ESP_OK;

fail:
    if (s_sensor_dev) {
        i2c_master_bus_rm_device(s_sensor_dev);
        s_sensor_dev = NULL;
    }
    (void)io_expander_write_pin(s_config.ioex_pwdn_port, s_config.ioex_pwdn_pin, true);
    s_initialized = false;
    return ret;
}

esp_err_t camera_hal_capture(uint8_t **out_data, size_t *out_len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_LOGW(TAG, "capture not implemented yet (DVP driver pending)");
    return ESP_ERR_NOT_SUPPORTED;
}

void camera_hal_release_frame(uint8_t *data)
{
    (void)data;
}

esp_err_t camera_hal_deinit(void)
{
    if (s_sensor_dev) {
        i2c_master_bus_rm_device(s_sensor_dev);
        s_sensor_dev = NULL;
    }
    io_expander_write_pin(s_config.ioex_pwdn_port, s_config.ioex_pwdn_pin, true);
    s_initialized = false;
    ESP_LOGI(TAG, "camera HAL deinitialized");
    return ESP_OK;
}

static inline int clamp_u8(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

esp_err_t camera_hal_yuv422_crop_to_rgb565(
    const uint8_t *src_yuv422, uint32_t src_w, uint32_t src_h,
    uint16_t *dst_rgb565, uint32_t dst_w, uint32_t dst_h)
{
    ESP_RETURN_ON_FALSE(NULL != src_yuv422 && NULL != dst_rgb565, ESP_ERR_INVALID_ARG, TAG, "null");
    ESP_RETURN_ON_FALSE(dst_w <= src_w && dst_h <= src_h, ESP_ERR_INVALID_ARG, TAG, "crop size");

    uint32_t x_off = (src_w - dst_w) / 2;
    uint32_t y_off = (src_h - dst_h) / 2;
    uint32_t src_stride = src_w * 2;

    for (uint32_t row = 0; row < dst_h; row++) {
        const uint8_t *src_line = src_yuv422 + (y_off + row) * src_stride + x_off * 2;
        uint16_t *dst_line = dst_rgb565 + row * dst_w;
        for (uint32_t col = 0; col < dst_w; col += 2) {
            uint8_t y0 = src_line[col * 2 + 0];
            uint8_t u  = src_line[col * 2 + 1];
            uint8_t y1 = src_line[col * 2 + 2];
            uint8_t v  = src_line[col * 2 + 3];
            int c0 = y0 - 16, c1 = y1 - 16;
            int d = u - 128, e = v - 128;
            int r0 = clamp_u8((298 * c0 + 409 * e + 128) >> 8);
            int g0 = clamp_u8((298 * c0 - 100 * d - 208 * e + 128) >> 8);
            int b0 = clamp_u8((298 * c0 + 516 * d + 128) >> 8);
            int r1 = clamp_u8((298 * c1 + 409 * e + 128) >> 8);
            int g1 = clamp_u8((298 * c1 - 100 * d - 208 * e + 128) >> 8);
            int b1 = clamp_u8((298 * c1 + 516 * d + 128) >> 8);
            dst_line[col] = (uint16_t)(((r0 >> 3) << 11) | ((g0 >> 2) << 5) | (b0 >> 3));
            dst_line[col + 1] = (uint16_t)(((r1 >> 3) << 11) | ((g1 >> 2) << 5) | (b1 >> 3));
        }
    }
    return ESP_OK;
}
