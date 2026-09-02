#pragma once

#include "holocubic_spectrum_raster.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

esp_err_t holocubic_spectrum_start(void);
bool holocubic_spectrum_snapshot(holocubic_spectrum_snapshot_t *snapshot);
void holocubic_spectrum_draw(uint16_t *canvas,
                             const holocubic_spectrum_snapshot_t *snapshot,
                             holocubic_spectrum_mode_t mode,
                             uint32_t now_ms);
