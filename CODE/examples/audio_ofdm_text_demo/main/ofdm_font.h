#ifndef OFDM_FONT_H
#define OFDM_FONT_H

#include "esp_err.h"
#include "lvgl.h"

extern const lv_font_t g_ofdm_font_noto_sans_sc_16;

esp_err_t ofdm_font_init(void);

#endif
