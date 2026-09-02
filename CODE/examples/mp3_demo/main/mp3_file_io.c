#include "mp3_file_io.h"

#include "esp_heap_caps.h"

#include <stdio.h>

#define MP3_FILE_LOCK_TIMEOUT_MS 1000U
#define MP3_FILE_READ_CHUNK_BYTES (16U * 1024U)

static esp_err_t locked_fopen(const char *path, mp3_spi_lock_t *spi_lock,
                              FILE **file)
{
    if (NULL == path || NULL == spi_lock || NULL == file) {
        return ESP_ERR_INVALID_ARG;
    }
    *file = NULL;
    if (!mp3_spi_lock_acquire(spi_lock, MP3_FILE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    *file = fopen(path, "rb");
    mp3_spi_lock_release(spi_lock);
    return (NULL != *file) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t locked_fseek(FILE *file, long offset, int origin,
                              mp3_spi_lock_t *spi_lock)
{
    if (NULL == file || NULL == spi_lock) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mp3_spi_lock_acquire(spi_lock, MP3_FILE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    int result = fseek(file, offset, origin);
    mp3_spi_lock_release(spi_lock);
    return (0 == result) ? ESP_OK : ESP_FAIL;
}

static esp_err_t locked_ftell(FILE *file, mp3_spi_lock_t *spi_lock,
                              long *position)
{
    if (NULL == file || NULL == spi_lock || NULL == position) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mp3_spi_lock_acquire(spi_lock, MP3_FILE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    *position = ftell(file);
    mp3_spi_lock_release(spi_lock);
    return (0 <= *position) ? ESP_OK : ESP_FAIL;
}

static esp_err_t locked_fread(FILE *file, mp3_spi_lock_t *spi_lock,
                              uint8_t *data, size_t requested,
                              size_t *actual)
{
    if (NULL == file || NULL == spi_lock || NULL == data || NULL == actual) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mp3_spi_lock_acquire(spi_lock, MP3_FILE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    *actual = fread(data, 1U, requested, file);
    bool failed = (0U == *actual) && (0 != ferror(file));
    mp3_spi_lock_release(spi_lock);
    return failed ? ESP_FAIL : ESP_OK;
}

static esp_err_t locked_fclose(FILE **file, mp3_spi_lock_t *spi_lock)
{
    if (NULL == file || NULL == spi_lock) {
        return ESP_ERR_INVALID_ARG;
    }
    if (NULL == *file) {
        return ESP_OK;
    }
    if (!mp3_spi_lock_acquire(spi_lock, MP3_FILE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    int result = fclose(*file);
    *file = NULL;
    mp3_spi_lock_release(spi_lock);
    return (0 == result) ? ESP_OK : ESP_FAIL;
}

esp_err_t mp3_file_read_all(const char *path, mp3_spi_lock_t *spi_lock,
                            size_t maximum_bytes, bool append_nul,
                            uint8_t **data, size_t *data_size)
{
    FILE *file = NULL;
    uint8_t *buffer = NULL;
    long file_size = 0L;
    size_t offset = 0U;
    esp_err_t result = ESP_OK;

    if (NULL == path || '\0' == path[0] || NULL == spi_lock ||
        0U == maximum_bytes || NULL == data || NULL == data_size) {
        return ESP_ERR_INVALID_ARG;
    }
    *data = NULL;
    *data_size = 0U;

    result = locked_fopen(path, spi_lock, &file);
    if (ESP_OK == result) {
        result = locked_fseek(file, 0L, SEEK_END, spi_lock);
    }
    if (ESP_OK == result) {
        result = locked_ftell(file, spi_lock, &file_size);
    }
    if (ESP_OK == result &&
        ((size_t)file_size > maximum_bytes ||
         (append_nul && (size_t)file_size == SIZE_MAX))) {
        result = ESP_ERR_INVALID_SIZE;
    }
    if (ESP_OK == result) {
        result = locked_fseek(file, 0L, SEEK_SET, spi_lock);
    }
    if (ESP_OK == result) {
        size_t allocation_size = (size_t)file_size + (append_nul ? 1U : 0U);

        if (0U == allocation_size) {
            allocation_size = 1U;
        }
        buffer = heap_caps_malloc(allocation_size,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (NULL == buffer) {
            result = ESP_ERR_NO_MEM;
        }
    }
    while (ESP_OK == result && offset < (size_t)file_size) {
        size_t remaining = (size_t)file_size - offset;
        size_t requested = (remaining < MP3_FILE_READ_CHUNK_BYTES)
                               ? remaining
                               : MP3_FILE_READ_CHUNK_BYTES;
        size_t actual = 0U;

        result = locked_fread(file, spi_lock, buffer + offset, requested,
                              &actual);
        if (ESP_OK != result || 0U == actual) {
            result = (ESP_OK == result) ? ESP_ERR_INVALID_SIZE : result;
            break;
        }
        offset += actual;
    }
    if (ESP_OK == result && append_nul) {
        buffer[file_size] = '\0';
    }

    esp_err_t close_result = locked_fclose(&file, spi_lock);
    if (ESP_OK == result && ESP_OK != close_result) {
        result = close_result;
    }
    if (ESP_OK != result) {
        heap_caps_free(buffer);
        return result;
    }
    *data = buffer;
    *data_size = (size_t)file_size;
    return ESP_OK;
}
