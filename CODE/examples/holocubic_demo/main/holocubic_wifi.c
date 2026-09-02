#include "holocubic_wifi.h"

#include <stdint.h>
#include <string.h>

static bool is_hex_digit(uint8_t byte)
{
    return (byte >= (uint8_t)'0' && byte <= (uint8_t)'9') ||
           (byte >= (uint8_t)'a' && byte <= (uint8_t)'f') ||
           (byte >= (uint8_t)'A' && byte <= (uint8_t)'F');
}

bool holocubic_wifi_credentials_set(holocubic_wifi_credentials_t *credentials,
                                    const char *ssid,
                                    const char *password)
{
    const char *effective_password = (NULL != password) ? password : "";
    size_t ssid_length = 0U;
    size_t password_length = 0U;

    if (NULL == credentials || NULL == ssid) {
        return false;
    }
    ssid_length = strlen(ssid);
    password_length = strlen(effective_password);
    if (0U == ssid_length || ssid_length > HOLO_WIFI_SSID_MAX_BYTES ||
        password_length < 8U || password_length > HOLO_WIFI_PASSWORD_MAX_BYTES) {
        return false;
    }
    bool ssid_has_non_space = false;
    for (size_t index = 0U; index < password_length; ++index) {
        uint8_t byte = (uint8_t)effective_password[index];

        if (byte < 0x20U || byte > 0x7EU) {
            return false;
        }
    }
    for (size_t index = 0U; index < ssid_length; ++index) {
        uint8_t byte = (uint8_t)ssid[index];
        if (byte <= 0x1FU || byte == 0x7FU) {
            return false;
        }
        if (byte != (uint8_t)' ') {
            ssid_has_non_space = true;
        }
    }
    if (!ssid_has_non_space) {
        return false;
    }
    if (password_length == HOLO_WIFI_PASSWORD_MAX_BYTES) {
        for (size_t index = 0U; index < password_length; ++index) {
            if (!is_hex_digit((uint8_t)effective_password[index])) {
                return false;
            }
        }
    } else {
        bool password_has_non_space = false;
        for (size_t index = 0U; index < password_length; ++index) {
            if (effective_password[index] != ' ') {
                password_has_non_space = true;
                break;
            }
        }
        if (!password_has_non_space) {
            return false;
        }
    }
    memset(credentials, 0, sizeof(*credentials));
    memcpy(credentials->ssid, ssid, ssid_length);
    memcpy(credentials->password, effective_password, password_length);
    return true;
}

bool holocubic_wifi_read_line(FILE *file, char *buffer, size_t capacity)
{
    size_t length = 0U;
    int next_char = EOF;

    if (NULL == file || NULL == buffer || capacity < 2U ||
        NULL == fgets(buffer, (int)capacity, file)) {
        return false;
    }
    length = strlen(buffer);
    if (0U < length && ('\n' == buffer[length - 1U] ||
                        '\r' == buffer[length - 1U])) {
        return true;
    }
    if (feof(file)) {
        return true;
    }
    do {
        next_char = fgetc(file);
    } while (EOF != next_char && '\n' != next_char);
    return false;
}

void holocubic_wifi_trim_line(char *line)
{
    size_t length = 0U;

    if (NULL == line) {
        return;
    }
    length = strlen(line);
    while (0U < length && ('\r' == line[length - 1U] || '\n' == line[length - 1U])) {
        line[--length] = '\0';
    }
}
