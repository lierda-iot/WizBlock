#include "mp3_catalog_scan.h"

#include "esp_log.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "mp3_catalog";

#define MP3_SCAN_LOCK_TIMEOUT_MS 1000U
#define MP3_SCAN_ENTRY_NAME_BYTES 256U

static bool copy_next_entry(DIR *directory, mp3_spi_lock_t *spi_lock,
                            char *name, size_t name_size, bool *has_entry)
{
    struct dirent *entry = NULL;

    if (NULL == directory || NULL == spi_lock || NULL == name ||
        0U == name_size || NULL == has_entry) {
        return false;
    }
    *has_entry = false;
    if (!mp3_spi_lock_acquire(spi_lock, MP3_SCAN_LOCK_TIMEOUT_MS)) {
        return false;
    }
    entry = readdir(directory);
    if (NULL != entry) {
        size_t length = strnlen(entry->d_name, name_size);

        if (length >= name_size) {
            mp3_spi_lock_release(spi_lock);
            return false;
        }
        memcpy(name, entry->d_name, length + 1U);
        *has_entry = true;
    }
    mp3_spi_lock_release(spi_lock);
    return true;
}

static bool path_is_type(const char *path, mp3_spi_lock_t *spi_lock,
                         bool want_directory)
{
    struct stat status = {0};
    int result = -1;

    if (NULL == path || NULL == spi_lock ||
        !mp3_spi_lock_acquire(spi_lock, MP3_SCAN_LOCK_TIMEOUT_MS)) {
        return false;
    }
    result = stat(path, &status);
    mp3_spi_lock_release(spi_lock);
    if (0 != result) {
        return false;
    }
    return want_directory ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode);
}

static bool make_path(char *path, size_t path_size, const char *root_path,
                      const char *directory_name, const char *file_name)
{
    int written = 0;

    if (NULL == path || 0U == path_size || NULL == root_path ||
        NULL == directory_name) {
        return false;
    }
    if (NULL == file_name) {
        written = snprintf(path, path_size, "%s/%s", root_path,
                           directory_name);
    } else {
        written = snprintf(path, path_size, "%s/%s/%s", root_path,
                           directory_name, file_name);
    }
    return written >= 0 && (size_t)written < path_size;
}

esp_err_t mp3_catalog_scan(const char *root_path, mp3_spi_lock_t *spi_lock,
                           mp3_catalog_t *catalog)
{
    DIR *directory = NULL;
    esp_err_t result = ESP_OK;

    if (NULL == root_path || NULL == spi_lock || NULL == catalog) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mp3_spi_lock_acquire(spi_lock, MP3_SCAN_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    directory = opendir(root_path);
    mp3_spi_lock_release(spi_lock);
    if (NULL == directory) {
        return ESP_ERR_NOT_FOUND;
    }

    while (ESP_OK == result) {
        char name[MP3_SCAN_ENTRY_NAME_BYTES] = {0};
        char directory_path[MP3_PATH_MAX_BYTES + 1U] = {0};
        char audio_path[MP3_PATH_MAX_BYTES + 1U] = {0};
        char lrc_path[MP3_PATH_MAX_BYTES + 1U] = {0};
        char cover_path[MP3_PATH_MAX_BYTES + 1U] = {0};
        bool has_entry = false;
        bool is_directory = false;
        bool has_audio = false;
        bool has_lrc = false;
        bool has_cover = false;
        mp3_catalog_reject_reason_t reject_reason =
            MP3_CATALOG_REJECT_NONE;

        if (!copy_next_entry(directory, spi_lock, name, sizeof(name),
                             &has_entry)) {
            result = ESP_FAIL;
            break;
        }
        if (!has_entry) {
            break;
        }
        if (0 == strcmp(name, ".") || 0 == strcmp(name, "..")) {
            continue;
        }

        if (make_path(directory_path, sizeof(directory_path), root_path, name,
                      NULL)) {
            is_directory = path_is_type(directory_path, spi_lock, true);
        }
        if (is_directory &&
            make_path(audio_path, sizeof(audio_path), root_path, name,
                      "audio.mp3")) {
            has_audio = path_is_type(audio_path, spi_lock, false);
        }
        if (is_directory &&
            make_path(lrc_path, sizeof(lrc_path), root_path, name,
                      "lyrics.lrc")) {
            has_lrc = path_is_type(lrc_path, spi_lock, false);
        }
        if (is_directory &&
            make_path(cover_path, sizeof(cover_path), root_path, name,
                      "cover.jpg")) {
            has_cover = path_is_type(cover_path, spi_lock, false);
        }

        mp3_catalog_add_result_t add_result = mp3_catalog_add_candidate(
            catalog, root_path, name, is_directory, has_audio, has_lrc,
            has_cover, &reject_reason);
        if (MP3_CATALOG_ADD_REJECTED == add_result) {
            ESP_LOGW(TAG, "SCAN reject=%d name=%s", (int)reject_reason, name);
        } else if (MP3_CATALOG_ADD_STORED == add_result) {
            ESP_LOGI(TAG, "SCAN accept name=%s lrc=%d cover=%d", name,
                     has_lrc, has_cover);
        }
    }

    if (!mp3_spi_lock_acquire(spi_lock, MP3_SCAN_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    if (0 != closedir(directory)) {
        result = ESP_FAIL;
    }
    mp3_spi_lock_release(spi_lock);
    return result;
}
