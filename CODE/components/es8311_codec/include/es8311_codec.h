#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t es8311_init(void);
esp_err_t es8311_start(void);
esp_err_t es8311_stop(void);
esp_err_t es8311_set_volume(uint8_t volume);
