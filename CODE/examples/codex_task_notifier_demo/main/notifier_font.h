#pragma once

#include "esp_err.h"
#include "lvgl.h"

extern const lv_font_t g_notifier_font_noto_sans_sc_16;

esp_err_t notifier_font_init(void);
