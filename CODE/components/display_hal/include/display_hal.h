#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"

typedef esp_err_t (*display_hal_set_level_fn)(void *ctx, bool level);

typedef struct {
    spi_host_device_t spi_host;
    int sclk_gpio_num;
    int mosi_gpio_num;
    int miso_gpio_num;
    int cs_gpio_num;
    int dc_gpio_num;
    int backlight_gpio_num;
    bool backlight_on_level;
    display_hal_set_level_fn set_reset_level;
    void *reset_ctx;
    bool reset_assert_level;
    int h_res;
    int v_res;
    int pixel_clock_hz;
    int draw_buffer_lines;
    bool color_invert;
    bool mirror_x;
    bool mirror_y;
    bool swap_xy;
} display_hal_config_t;

#define DISPLAY_HAL_RGB565_RED 0xF800
#define DISPLAY_HAL_RGB565_WHITE 0xFFFF

esp_err_t display_hal_init(const display_hal_config_t *config);
esp_err_t display_hal_deinit(void);
esp_err_t display_hal_probe(void);
esp_err_t display_hal_fill_rgb565(uint16_t color);
esp_err_t display_hal_draw_bitmap_rgb565(int x, int y, int w, int h, const uint16_t *data);
esp_err_t display_hal_wait_pending(int timeout_ms);
esp_err_t display_hal_set_orientation(bool swap_xy, bool mirror_x, bool mirror_y);
