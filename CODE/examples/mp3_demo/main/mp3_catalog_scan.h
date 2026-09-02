#pragma once

#include "mp3_catalog.h"
#include "mp3_spi_lock.h"

#include "esp_err.h"

esp_err_t mp3_catalog_scan(const char *root_path, mp3_spi_lock_t *spi_lock,
                           mp3_catalog_t *catalog);
