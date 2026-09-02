/* Validation and canonical copying for length-delimited Wi-Fi credentials. */
#include "network_manager_wifi_config.h"

#include <stddef.h>

static bool is_hex_digit(uint8_t value)
{
    return ('0' <= value && '9' >= value) ||
           ('a' <= value && 'f' >= value) ||
           ('A' <= value && 'F' >= value);
}

bool network_manager_wifi_config_is_valid(
    const network_manager_wifi_config_t *config)
{
    if (NULL == config) {
        return false;
    }
    if (0U == config->ssid_len ||
        NETWORK_MANAGER_WIFI_SSID_MAX_BYTES < config->ssid_len) {
        return false;
    }
    bool ssid_has_non_space = false;
    for (uint8_t index = 0U; index < config->ssid_len; ++index) {
        if (config->ssid[index] <= 0x1FU || 0x7FU == config->ssid[index]) {
            return false;
        }
        if (0x20U != config->ssid[index]) {
            ssid_has_non_space = true;
        }
    }
    if (!ssid_has_non_space) {
        return false;
    }
    if (8U > config->password_len ||
        NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES < config->password_len) {
        return false;
    }
    /* Lengths below 64 are passphrases; exactly 64 bytes is a raw hex PSK. */
    if (NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES != config->password_len) {
        bool password_has_non_space = false;

        for (uint8_t index = 0U; index < config->password_len; ++index) {
            if (0x20U > config->password[index] ||
                0x7EU < config->password[index]) {
                return false;
            }
            if (0x20U != config->password[index]) {
                password_has_non_space = true;
            }
        }
        return password_has_non_space;
    }
    for (uint8_t index = 0U; index < config->password_len; ++index) {
        if (!is_hex_digit(config->password[index])) {
            return false;
        }
    }
    return true;
}

bool network_manager_wifi_config_copy(
    network_manager_wifi_config_t *destination,
    const network_manager_wifi_config_t *source)
{
    if (NULL == destination || !network_manager_wifi_config_is_valid(source)) {
        return false;
    }

    /* Zero the unused tail so persistence and comparisons are deterministic. */
    network_manager_wifi_config_t sanitized = {0};

    sanitized.ssid_len = source->ssid_len;
    sanitized.password_len = source->password_len;
    for (uint8_t index = 0U; index < source->ssid_len; ++index) {
        sanitized.ssid[index] = source->ssid[index];
    }
    for (uint8_t index = 0U; index < source->password_len; ++index) {
        sanitized.password[index] = source->password[index];
    }
    *destination = sanitized;
    return true;
}
