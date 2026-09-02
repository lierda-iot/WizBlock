#pragma once

#include "mp3_spi_lock.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t mp3_file_read_all(const char *path, mp3_spi_lock_t *spi_lock,
                            size_t maximum_bytes, bool append_nul,
                            uint8_t **data, size_t *data_size);
