#include "firmware_package.h"

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "firmware_package";

#define FIRMWARE_PACKAGE_MANIFEST_MAX_BYTES 4096
#define FIRMWARE_PACKAGE_IO_BUFFER_SIZE 8192
#define FIRMWARE_PACKAGE_BOARD "laiwfs300"
#define FIRMWARE_PACKAGE_PARTITION_SCHEME "demo-hub-v2"

static bool is_directory(const char *path)
{
    struct stat file_stat = {0};
    return stat(path, &file_stat) == 0 && S_ISDIR(file_stat.st_mode);
}

static bool is_regular_file(const char *path, struct stat *out_stat)
{
    struct stat file_stat = {0};
    if (stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        return false;
    }
    if (NULL != out_stat) {
        *out_stat = file_stat;
    }
    return true;
}

static bool join_path(char *out_path, size_t out_size, const char *base, const char *name)
{
    if (NULL == out_path || 0 == out_size || NULL == base || NULL == name ||
        0 == name[0] || name[0] == '/') {
        return false;
    }

    int written = snprintf(out_path, out_size, "%s/%s", base, name);
    return written > 0 && (size_t)written < out_size;
}

static bool has_safe_relative_path(const char *path)
{
    if (NULL == path || 0 == path[0] || path[0] == '/' || NULL != strstr(path, "\\")) {
        return false;
    }

    const char *cursor = path;
    while (*cursor != '\0') {
        const char *separator = strchr(cursor, '/');
        size_t length = (NULL == separator) ? strlen(cursor) : (size_t)(separator - cursor);
        if (0 == length || (2 == length && 0 == strncmp(cursor, "..", length))) {
            return false;
        }
        cursor = (NULL == separator) ? cursor + length : separator + 1;
    }
    return true;
}

static bool copy_json_string(cJSON *object, const char *name, char *out, size_t out_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (NULL == item || !cJSON_IsString(item) || NULL == item->valuestring || 0 == item->valuestring[0]) {
        return false;
    }

    size_t length = strlen(item->valuestring);
    if (length >= out_size) {
        return false;
    }
    memcpy(out, item->valuestring, length + 1U);
    return true;
}

static bool copy_json_uint32(cJSON *object, const char *name, uint32_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (NULL == item || !cJSON_IsNumber(item) || item->valuedouble < 1.0 ||
        item->valuedouble > UINT32_MAX || item->valuedouble != (double)item->valueint) {
        return false;
    }
    *out = (uint32_t)item->valueint;
    return true;
}

static bool is_sha256_hex(const char *value)
{
    if (NULL == value || FIRMWARE_PACKAGE_SHA256_HEX_LEN != strlen(value)) {
        return false;
    }
    for (size_t index = 0; index < FIRMWARE_PACKAGE_SHA256_HEX_LEN; ++index) {
        if (!isxdigit((unsigned char)value[index])) {
            return false;
        }
    }
    return true;
}

static bool is_supported_resource_partition(const char *label)
{
    return NULL != label &&
           (0 == strcmp("model", label) || 0 == strcmp("spiffs_data", label));
}

static bool parse_resource_images(cJSON *root,
                                  const char *package_dir,
                                  firmware_package_t *package)
{
    cJSON *partitions = cJSON_GetObjectItemCaseSensitive(root, "partitions");
    if (NULL == partitions) {
        return true;
    }
    if (!cJSON_IsArray(partitions)) {
        return false;
    }

    int resource_count = cJSON_GetArraySize(partitions);
    if (resource_count < 0 || resource_count > FIRMWARE_PACKAGE_RESOURCE_MAX) {
        return false;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, partitions) {
        firmware_package_resource_t resource = {0};
        bool valid = cJSON_IsObject(item) &&
                     copy_json_string(item, "label", resource.partition_label,
                                      sizeof(resource.partition_label)) &&
                     copy_json_string(item, "file", resource.file_path,
                                      sizeof(resource.file_path)) &&
                     copy_json_string(item, "sha256", resource.sha256,
                                      sizeof(resource.sha256)) &&
                     copy_json_uint32(item, "size", &resource.size) &&
                     is_supported_resource_partition(resource.partition_label) &&
                     has_safe_relative_path(resource.file_path) &&
                     is_sha256_hex(resource.sha256);
        if (!valid) {
            return false;
        }

        for (size_t index = 0; index < package->resource_count; ++index) {
            if (0 == strcmp(package->resources[index].partition_label,
                            resource.partition_label)) {
                return false;
            }
        }

        char file_path[FIRMWARE_PACKAGE_PATH_MAX] = {0};
        struct stat file_stat = {0};
        if (!join_path(file_path, sizeof(file_path), package_dir, resource.file_path) ||
            !is_regular_file(file_path, &file_stat) || file_stat.st_size <= 0 ||
            (uint64_t)file_stat.st_size != resource.size) {
            return false;
        }

        memcpy(resource.file_path, file_path, strlen(file_path) + 1U);
        package->resources[package->resource_count] = resource;
        package->resource_count++;
    }
    return true;
}

static esp_err_t read_manifest(const char *package_dir, firmware_package_t *package)
{
    char manifest_path[FIRMWARE_PACKAGE_PATH_MAX] = {0};
    if (!join_path(manifest_path, sizeof(manifest_path), package_dir, "manifest.json")) {
        return ESP_ERR_INVALID_SIZE;
    }

    struct stat manifest_stat = {0};
    if (!is_regular_file(manifest_path, &manifest_stat) ||
        manifest_stat.st_size <= 0 || manifest_stat.st_size > FIRMWARE_PACKAGE_MANIFEST_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(manifest_path, "rb");
    if (NULL == file) {
        return ESP_FAIL;
    }

    size_t json_size = (size_t)manifest_stat.st_size;
    char *json_text = calloc(json_size + 1U, sizeof(char));
    if (NULL == json_text) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_size = fread(json_text, 1U, json_size, file);
    fclose(file);
    if (json_size != read_size) {
        free(json_text);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_ParseWithLength(json_text, json_size);
    free(json_text);
    if (NULL == root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    firmware_package_t parsed = {0};
    bool valid = copy_json_string(root, "id", parsed.id, sizeof(parsed.id)) &&
                 copy_json_string(root, "name", parsed.name, sizeof(parsed.name)) &&
                 copy_json_string(root, "version", parsed.version, sizeof(parsed.version)) &&
                 copy_json_string(root, "app", parsed.app_path, sizeof(parsed.app_path)) &&
                 copy_json_string(root, "app_sha256", parsed.app_sha256, sizeof(parsed.app_sha256)) &&
                 copy_json_uint32(root, "app_size", &parsed.app_size);

    cJSON *board = cJSON_GetObjectItemCaseSensitive(root, "board");
    cJSON *partition_scheme = cJSON_GetObjectItemCaseSensitive(root, "partition_scheme");
    valid = valid && cJSON_IsString(board) && cJSON_IsString(partition_scheme) &&
            0 == strcmp(FIRMWARE_PACKAGE_BOARD, board->valuestring) &&
            0 == strcmp(FIRMWARE_PACKAGE_PARTITION_SCHEME, partition_scheme->valuestring) &&
            is_sha256_hex(parsed.app_sha256) && has_safe_relative_path(parsed.app_path);

    if (valid) {
        size_t package_dir_len = strlen(package_dir);
        if (package_dir_len >= sizeof(parsed.package_dir)) {
            valid = false;
        } else {
            memcpy(parsed.package_dir, package_dir, package_dir_len + 1U);
        }
    }

    struct stat app_stat = {0};
    char app_path[FIRMWARE_PACKAGE_PATH_MAX] = {0};
    if (valid && !join_path(app_path, sizeof(app_path), parsed.package_dir, parsed.app_path)) {
        valid = false;
    }
    if (valid && (!is_regular_file(app_path, &app_stat) || app_stat.st_size < 0 ||
                  (uint64_t)app_stat.st_size != parsed.app_size)) {
        valid = false;
    }
    if (valid && !parse_resource_images(root, parsed.package_dir, &parsed)) {
        valid = false;
    }
    if (valid) {
        memcpy(parsed.app_path, app_path, strlen(app_path) + 1U);
        *package = parsed;
    }

    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static int compare_packages(const void *left, const void *right)
{
    const firmware_package_t *left_package = left;
    const firmware_package_t *right_package = right;
    int id_result = strcmp(left_package->id, right_package->id);
    return (0 != id_result) ? id_result : strcmp(left_package->version, right_package->version);
}

esp_err_t firmware_package_scan(const char *packages_root,
                                firmware_package_t *packages,
                                size_t capacity,
                                size_t *package_count,
                                size_t *rejected_count)
{
    ESP_RETURN_ON_FALSE(NULL != packages_root && NULL != packages && capacity > 0 &&
                            NULL != package_count && NULL != rejected_count,
                        ESP_ERR_INVALID_ARG, TAG, "invalid scan arguments");

    *package_count = 0;
    *rejected_count = 0;

    DIR *root_dir = opendir(packages_root);
    if (NULL == root_dir) {
        ESP_LOGW(TAG, "cannot open package root: %s (%s)", packages_root, strerror(errno));
        return (ENOENT == errno) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    struct dirent *demo_entry = NULL;
    while (NULL != (demo_entry = readdir(root_dir))) {
        if ('.' == demo_entry->d_name[0]) {
            continue;
        }

        char demo_dir[FIRMWARE_PACKAGE_PATH_MAX] = {0};
        ESP_LOGI(TAG, "found demo directory entry: %s", demo_entry->d_name);
        if (!join_path(demo_dir, sizeof(demo_dir), packages_root, demo_entry->d_name) ||
            !is_directory(demo_dir)) {
            ESP_LOGW(TAG, "skip non-directory demo entry: %s", demo_entry->d_name);
            continue;
        }

        DIR *version_dir = opendir(demo_dir);
        if (NULL == version_dir) {
            ESP_LOGW(TAG, "reject demo directory %s: cannot open versions (%s)",
                     demo_dir, strerror(errno));
            (*rejected_count)++;
            continue;
        }

        struct dirent *version_entry = NULL;
        while (NULL != (version_entry = readdir(version_dir))) {
            if ('.' == version_entry->d_name[0]) {
                continue;
            }

            char package_dir[FIRMWARE_PACKAGE_PATH_MAX] = {0};
            if (!join_path(package_dir, sizeof(package_dir), demo_dir, version_entry->d_name) ||
                !is_directory(package_dir)) {
                continue;
            }

            char ready_path[FIRMWARE_PACKAGE_PATH_MAX] = {0};
            if (!join_path(ready_path, sizeof(ready_path), package_dir, "READY") ||
                !is_regular_file(ready_path, NULL)) {
                ESP_LOGW(TAG, "reject package %s: READY missing", package_dir);
                (*rejected_count)++;
                continue;
            }

            firmware_package_t package = {0};
            esp_err_t manifest_ret = read_manifest(package_dir, &package);
            if (ESP_OK != manifest_ret) {
                ESP_LOGW(TAG, "reject package %s: manifest invalid (%s)",
                         package_dir, esp_err_to_name(manifest_ret));
                (*rejected_count)++;
                continue;
            }

            if (*package_count >= capacity) {
                (*rejected_count)++;
                continue;
            }
            packages[*package_count] = package;
            (*package_count)++;
            ESP_LOGI(TAG, "accepted package %s v%s", package.id, package.version);
        }
        closedir(version_dir);
    }
    closedir(root_dir);

    qsort(packages, *package_count, sizeof(packages[0]), compare_packages);
    ESP_LOGI(TAG, "package scan complete: accepted=%u rejected=%u",
             (unsigned)*package_count, (unsigned)*rejected_count);
    return ESP_OK;
}

static void report_progress(firmware_package_progress_cb_t progress_cb,
                            int percent,
                            const char *stage,
                            void *user_ctx)
{
    if (NULL != progress_cb) {
        progress_cb(percent, stage, user_ctx);
    }
}

static esp_err_t calculate_sha256(const char *path,
                                  unsigned char digest[32],
                                  firmware_package_progress_cb_t progress_cb,
                                  void *user_ctx)
{
    struct stat file_stat = {0};
    ESP_RETURN_ON_FALSE(is_regular_file(path, &file_stat), ESP_ERR_NOT_FOUND, TAG, "package file missing");

    FILE *file = fopen(path, "rb");
    ESP_RETURN_ON_FALSE(NULL != file, ESP_FAIL, TAG, "open app");

    unsigned char *buffer = malloc(FIRMWARE_PACKAGE_IO_BUFFER_SIZE);
    if (NULL == buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    mbedtls_sha256_context sha_context;
    mbedtls_sha256_init(&sha_context);
    esp_err_t ret = (0 == mbedtls_sha256_starts(&sha_context, 0)) ? ESP_OK : ESP_FAIL;
    uint64_t processed = 0;
    while (ESP_OK == ret) {
        size_t read_size = fread(buffer, 1U, FIRMWARE_PACKAGE_IO_BUFFER_SIZE, file);
        if (read_size > 0) {
            if (0 != mbedtls_sha256_update(&sha_context, buffer, read_size)) {
                ret = ESP_FAIL;
                break;
            }
            processed += read_size;
            int percent = (file_stat.st_size > 0) ? (int)(processed * 30U / file_stat.st_size) : 30;
            report_progress(progress_cb, percent, "Verifying", user_ctx);
        }
        if (read_size < FIRMWARE_PACKAGE_IO_BUFFER_SIZE) {
            if (ferror(file)) {
                ret = ESP_FAIL;
            }
            break;
        }
    }

    if (ESP_OK == ret && 0 != mbedtls_sha256_finish(&sha_context, digest)) {
        ret = ESP_FAIL;
    }
    mbedtls_sha256_free(&sha_context);
    free(buffer);
    fclose(file);
    return ret;
}

static void digest_to_hex(const unsigned char digest[32], char output[65])
{
    static const char hex[] = "0123456789abcdef";
    for (size_t index = 0; index < 32; ++index) {
        output[index * 2U] = hex[(digest[index] >> 4) & 0x0f];
        output[index * 2U + 1U] = hex[digest[index] & 0x0f];
    }
    output[64] = '\0';
}

static esp_err_t verify_file_sha256(const char *path,
                                    uint32_t expected_size,
                                    const char *expected_sha256,
                                    firmware_package_progress_cb_t progress_cb,
                                    void *user_ctx)
{
    struct stat file_stat = {0};
    ESP_RETURN_ON_FALSE(is_regular_file(path, &file_stat), ESP_ERR_NOT_FOUND,
                        TAG, "package file missing");
    ESP_RETURN_ON_FALSE((uint64_t)file_stat.st_size == expected_size, ESP_ERR_INVALID_SIZE,
                        TAG, "package file size mismatch");

    unsigned char digest[32] = {0};
    esp_err_t ret = calculate_sha256(path, digest, progress_cb, user_ctx);
    if (ESP_OK != ret) {
        return ret;
    }

    char digest_hex[65] = {0};
    digest_to_hex(digest, digest_hex);
    if (0 != strcasecmp(digest_hex, expected_sha256)) {
        ESP_LOGE(TAG, "SHA-256 mismatch for %s", path);
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

esp_err_t firmware_package_verify(const firmware_package_t *package,
                                  firmware_package_progress_cb_t progress_cb,
                                  void *user_ctx)
{
    ESP_RETURN_ON_FALSE(NULL != package, ESP_ERR_INVALID_ARG, TAG, "missing package");

    esp_err_t ret = verify_file_sha256(package->app_path, package->app_size,
                                       package->app_sha256, progress_cb, user_ctx);
    if (ESP_OK != ret) {
        return ret;
    }

    for (size_t index = 0; index < package->resource_count; ++index) {
        const firmware_package_resource_t *resource = &package->resources[index];
        ret = verify_file_sha256(resource->file_path, resource->size, resource->sha256,
                                 progress_cb, user_ctx);
        if (ESP_OK != ret) {
            return ret;
        }
    }
    report_progress(progress_cb, 30, "Verified", user_ctx);
    return ESP_OK;
}

static const esp_partition_t *find_resource_partition(
    const firmware_package_resource_t *resource)
{
    if (NULL == resource || !is_supported_resource_partition(resource->partition_label)) {
        return NULL;
    }
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                    resource->partition_label);
}

static esp_err_t install_resource_partition(const firmware_package_resource_t *resource,
                                            size_t resource_index,
                                            size_t resource_count,
                                            firmware_package_progress_cb_t progress_cb,
                                            void *user_ctx)
{
    const esp_partition_t *target = find_resource_partition(resource);
    ESP_RETURN_ON_FALSE(NULL != target, ESP_ERR_NOT_FOUND, TAG,
                        "resource partition missing");
    ESP_RETURN_ON_FALSE(resource->size <= target->size, ESP_ERR_INVALID_SIZE, TAG,
                        "resource does not fit partition");

    esp_err_t ret = esp_partition_erase_range(target, 0, target->size);
    if (ESP_OK != ret) {
        return ret;
    }

    FILE *file = fopen(resource->file_path, "rb");
    ESP_RETURN_ON_FALSE(NULL != file, ESP_FAIL, TAG, "open resource image");

    unsigned char *buffer = malloc(FIRMWARE_PACKAGE_IO_BUFFER_SIZE);
    if (NULL == buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    uint32_t written = 0;
    while (written < resource->size) {
        size_t remaining = resource->size - written;
        size_t request_size = remaining > FIRMWARE_PACKAGE_IO_BUFFER_SIZE
                                  ? FIRMWARE_PACKAGE_IO_BUFFER_SIZE
                                  : remaining;
        size_t read_size = fread(buffer, 1U, request_size, file);
        if (read_size != request_size) {
            ret = ESP_FAIL;
            break;
        }
        ret = esp_partition_write(target, written, buffer, read_size);
        if (ESP_OK != ret) {
            break;
        }
        written += (uint32_t)read_size;
        int percent = 30 + (int)(((resource_index * 15U) +
                                  (written * 15U / resource->size)) /
                                 resource_count);
        report_progress(progress_cb, percent, "Installing resources", user_ctx);
    }

    free(buffer);
    fclose(file);
    if (ESP_OK != ret) {
        return ret;
    }

    unsigned char *read_buffer = malloc(FIRMWARE_PACKAGE_IO_BUFFER_SIZE);
    ESP_RETURN_ON_FALSE(NULL != read_buffer, ESP_ERR_NO_MEM, TAG,
                        "allocate resource verify buffer");

    mbedtls_sha256_context sha_context;
    mbedtls_sha256_init(&sha_context);
    ret = (0 == mbedtls_sha256_starts(&sha_context, 0)) ? ESP_OK : ESP_FAIL;
    uint32_t offset = 0;
    while (ESP_OK == ret && offset < resource->size) {
        size_t read_size = resource->size - offset > FIRMWARE_PACKAGE_IO_BUFFER_SIZE
                               ? FIRMWARE_PACKAGE_IO_BUFFER_SIZE
                               : resource->size - offset;
        ret = esp_partition_read(target, offset, read_buffer, read_size);
        if (ESP_OK == ret && 0 != mbedtls_sha256_update(&sha_context, read_buffer, read_size)) {
            ret = ESP_FAIL;
        }
        offset += (uint32_t)read_size;
    }

    unsigned char digest[32] = {0};
    if (ESP_OK == ret && 0 != mbedtls_sha256_finish(&sha_context, digest)) {
        ret = ESP_FAIL;
    }
    mbedtls_sha256_free(&sha_context);
    free(read_buffer);
    if (ESP_OK != ret) {
        return ret;
    }

    char digest_hex[65] = {0};
    digest_to_hex(digest, digest_hex);
    return (0 == strcasecmp(digest_hex, resource->sha256)) ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t firmware_package_install(const firmware_package_t *package,
                                   firmware_package_progress_cb_t progress_cb,
                                   void *user_ctx)
{
    ESP_RETURN_ON_FALSE(NULL != package, ESP_ERR_INVALID_ARG, TAG, "missing package");

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_RETURN_ON_FALSE(NULL != running && ESP_PARTITION_SUBTYPE_APP_FACTORY == running->subtype,
                        ESP_ERR_INVALID_STATE, TAG, "launcher must run from factory");

    const esp_partition_t *target = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                               ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                                               NULL);
    ESP_RETURN_ON_FALSE(NULL != target, ESP_ERR_NOT_FOUND, TAG, "ota_0 partition missing");
    ESP_RETURN_ON_FALSE(package->app_size <= target->size, ESP_ERR_INVALID_SIZE,
                        TAG, "app does not fit ota_0");

    for (size_t index = 0; index < package->resource_count; ++index) {
        const esp_partition_t *resource_target = find_resource_partition(&package->resources[index]);
        ESP_RETURN_ON_FALSE(NULL != resource_target, ESP_ERR_NOT_FOUND, TAG,
                            "resource partition missing");
        ESP_RETURN_ON_FALSE(package->resources[index].size <= resource_target->size,
                            ESP_ERR_INVALID_SIZE, TAG, "resource does not fit partition");
    }

    esp_err_t ret = firmware_package_verify(package, progress_cb, user_ctx);
    if (ESP_OK != ret) {
        return ret;
    }

    for (size_t index = 0; index < package->resource_count; ++index) {
        ret = install_resource_partition(&package->resources[index], index,
                                         package->resource_count, progress_cb, user_ctx);
        if (ESP_OK != ret) {
            return ret;
        }
    }

    FILE *file = fopen(package->app_path, "rb");
    ESP_RETURN_ON_FALSE(NULL != file, ESP_FAIL, TAG, "open app for install");

    esp_ota_handle_t ota_handle = 0;
    ret = esp_ota_begin(target, package->app_size, &ota_handle);
    if (ESP_OK != ret) {
        fclose(file);
        return ret;
    }

    unsigned char *buffer = malloc(FIRMWARE_PACKAGE_IO_BUFFER_SIZE);
    if (NULL == buffer) {
        esp_ota_abort(ota_handle);
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    const int app_install_start = (package->resource_count > 0) ? 45 : 30;
    const int app_install_span = 95 - app_install_start;
    uint32_t written = 0;
    while (written < package->app_size) {
        size_t remaining = package->app_size - written;
        size_t request_size = remaining > FIRMWARE_PACKAGE_IO_BUFFER_SIZE
                                  ? FIRMWARE_PACKAGE_IO_BUFFER_SIZE
                                  : remaining;
        size_t read_size = fread(buffer, 1U, request_size, file);
        if (read_size != request_size) {
            ret = ESP_FAIL;
            break;
        }

        ret = esp_ota_write(ota_handle, buffer, read_size);
        if (ESP_OK != ret) {
            break;
        }
        written += (uint32_t)read_size;
        int percent = app_install_start + (int)(written * (uint32_t)app_install_span /
                                                package->app_size);
        report_progress(progress_cb, percent, "Installing", user_ctx);
    }

    free(buffer);
    fclose(file);

    if (ESP_OK != ret) {
        esp_ota_abort(ota_handle);
        return ret;
    }

    ret = esp_ota_end(ota_handle);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "OTA image validation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ota_set_boot_partition(target);
    if (ESP_OK == ret) {
        report_progress(progress_cb, 100, "Ready", user_ctx);
    }
    return ret;
}
