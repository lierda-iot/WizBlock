#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define FIRMWARE_PACKAGE_MAX_COUNT 16
#define FIRMWARE_PACKAGE_ID_MAX 48
#define FIRMWARE_PACKAGE_NAME_MAX 64
#define FIRMWARE_PACKAGE_VERSION_MAX 24
#define FIRMWARE_PACKAGE_PATH_MAX 256
#define FIRMWARE_PACKAGE_SHA256_HEX_LEN 64
#define FIRMWARE_PACKAGE_RESOURCE_MAX 2
#define FIRMWARE_PACKAGE_PARTITION_LABEL_MAX 16

typedef struct {
    char partition_label[FIRMWARE_PACKAGE_PARTITION_LABEL_MAX];
    char file_path[FIRMWARE_PACKAGE_PATH_MAX];
    char sha256[FIRMWARE_PACKAGE_SHA256_HEX_LEN + 1];
    uint32_t size;
} firmware_package_resource_t;

typedef struct {
    char id[FIRMWARE_PACKAGE_ID_MAX];
    char name[FIRMWARE_PACKAGE_NAME_MAX];
    char version[FIRMWARE_PACKAGE_VERSION_MAX];
    char package_dir[FIRMWARE_PACKAGE_PATH_MAX];
    char app_path[FIRMWARE_PACKAGE_PATH_MAX];
    char app_sha256[FIRMWARE_PACKAGE_SHA256_HEX_LEN + 1];
    uint32_t app_size;
    size_t resource_count;
    firmware_package_resource_t resources[FIRMWARE_PACKAGE_RESOURCE_MAX];
} firmware_package_t;

typedef void (*firmware_package_progress_cb_t)(int percent, const char *stage, void *user_ctx);

esp_err_t firmware_package_scan(const char *packages_root,
                                firmware_package_t *packages,
                                size_t capacity,
                                size_t *package_count,
                                size_t *rejected_count);
esp_err_t firmware_package_verify(const firmware_package_t *package,
                                  firmware_package_progress_cb_t progress_cb,
                                  void *user_ctx);
esp_err_t firmware_package_install(const firmware_package_t *package,
                                   firmware_package_progress_cb_t progress_cb,
                                   void *user_ctx);
