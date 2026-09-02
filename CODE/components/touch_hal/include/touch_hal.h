#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t weight;
    uint8_t event;
} touch_panel_point_t;

typedef struct {
    uint8_t chip_id;
    uint8_t firmware_ver;
    uint8_t lib_ver_h;
    uint8_t lib_ver_l;
} touch_panel_info_t;

esp_err_t touch_panel_init(void);
esp_err_t touch_panel_probe(void);
esp_err_t touch_panel_read_info(touch_panel_info_t *info);
esp_err_t touch_panel_read_point(touch_panel_point_t *point, uint8_t *touch_count);
