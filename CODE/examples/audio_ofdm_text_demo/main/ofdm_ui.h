#ifndef OFDM_UI_H
#define OFDM_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "ofdm_link.h"

#define OFDM_UI_LCD_PIXEL_CLOCK_HZ 20000000U
#define OFDM_UI_LCD_BUFFER_LINES 40U

esp_err_t ofdm_ui_init(bool touch_available);
void ofdm_ui_process(void);
void ofdm_ui_update(const ofdm_link_snapshot_t *snapshot);
uint32_t ofdm_ui_get_lcd_error_count(void);
uint32_t ofdm_ui_get_touch_error_count(void);

#endif
