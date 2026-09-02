#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t es7210_init(void);
esp_err_t es7210_start(void);
esp_err_t es7210_stop(void);
