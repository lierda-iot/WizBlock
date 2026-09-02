/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>

#include "esp_gmf_io.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  File IO configurations, if any entry is zero then the configuration will be set to default values
 */
typedef struct {
    int               dir;         /*!< IO direction, reader or writer */
    const char       *name;        /*!< Name for this instance */
    int               cache_size;  /*!< Cache size for file IO operations in bytes. If size <= 512, it will be set to 0.
                                          Note: Larger cache size will improve read and write performance but consume more memory */
    int               cache_caps;  /*!< Cache memory capabilities, if zero then it will be set to MALLOC_CAP_DMA.
                                          Note:
                                           1. If chips have SOC_SDMMC_PSRAM_DMA_CAPABLE capability(such as ESP32P4),
                                               then you can set (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA) to save SRAM
                                           2. For ESP32, should use (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) or MALLOC_CAP_DMA caps to malloc cache
                                           3. For ESP32Sxx and ESP32Cxx, can also use MALLOC_CAP_INTERNAL caps to malloc cache */
    esp_gmf_io_cfg_t  io_cfg;      /*!< IO configuration for task and buffer */
} file_io_cfg_t;

/**
 * @brief Callback used to serialize file operations with another SPI2 user.
 *
 * @param[in] acquire  true before a filesystem operation, false afterward
 * @param[in] context  application context registered with the callback
 *
 * @return true when the operation may proceed. The return value is ignored
 *         for release callbacks.
 */
typedef bool (*esp_gmf_io_file_lock_cb_t)(bool acquire, void *context);

#define FILE_IO_CFG_DEFAULT()  {          \
    .dir        = ESP_GMF_IO_DIR_READER,  \
    .name       = NULL,                   \
    .cache_size = 0,                      \
    .cache_caps = 0,                      \
    .io_cfg     = {                       \
        .thread = {                       \
            .stack        = 0,            \
            .prio         = 0,            \
            .core         = 0,            \
            .stack_in_ext = false,        \
        },                                \
        .buffer_cfg = {                   \
            .io_size     = 0,             \
            .buffer_size = 0,             \
            .read_filter = NULL,          \
        },                                \
        .enable_speed_monitor = false,    \
    },                                    \
}

/**
 * @brief  Initializes the file stream I/O with the provided configuration
 *
 * @param[in]   config  Pointer to the file IO configuration
 * @param[out]  io      Pointer to the file IO handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_io_file_init(file_io_cfg_t *config, esp_gmf_io_handle_t *io);

/**
 * @brief Set the process-wide file operation lock callback.
 *
 * Register this before creating any file IO instance and do not replace it
 * while an instance is active. Passing NULL disables locking.
 */
void esp_gmf_io_file_set_lock_callback(esp_gmf_io_file_lock_cb_t callback,
                                       void *context);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
