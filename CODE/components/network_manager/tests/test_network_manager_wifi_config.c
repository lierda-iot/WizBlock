#include "network_manager.h"
#include "network_manager_wifi_config.h"

static network_manager_wifi_config_t make_valid_config(void)
{
    network_manager_wifi_config_t config = {0};

    config.ssid[0] = 'a';
    config.ssid_len = 1U;
    for (uint8_t index = 0U; index < 8U; ++index) {
        config.password[index] = 'x';
    }
    config.password_len = 8U;

    return config;
}

static bool configs_equal(const network_manager_wifi_config_t *left,
                          const network_manager_wifi_config_t *right)
{
    if (left->ssid_len != right->ssid_len ||
        left->password_len != right->password_len) {
        return false;
    }
    for (uint8_t index = 0U;
         index < NETWORK_MANAGER_WIFI_SSID_MAX_BYTES;
         ++index) {
        if (left->ssid[index] != right->ssid[index]) {
            return false;
        }
    }
    for (uint8_t index = 0U;
         index < NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES;
         ++index) {
        if (left->password[index] != right->password[index]) {
            return false;
        }
    }
    return true;
}

int main(void)
{
    const network_manager_wifi_config_t config = make_valid_config();

    if (!network_manager_wifi_config_is_valid(&config)) {
        return 1;
    }
    if (network_manager_wifi_config_is_valid(
            (const network_manager_wifi_config_t *)0)) {
        return 2;
    }

    network_manager_wifi_config_t invalid = make_valid_config();
    invalid.ssid_len = 0U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 3;
    }

    invalid = make_valid_config();
    invalid.ssid_len = NETWORK_MANAGER_WIFI_SSID_MAX_BYTES + 1U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 4;
    }

    invalid = make_valid_config();
    invalid.ssid[0] = 0x00U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 5;
    }

    invalid.ssid[0] = 0x1FU;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 6;
    }

    invalid.ssid[0] = 0x7FU;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 7;
    }

    invalid = make_valid_config();
    invalid.ssid_len = 3U;
    invalid.ssid[0] = 0x20U;
    invalid.ssid[1] = 0x20U;
    invalid.ssid[2] = 0x20U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 8;
    }

    invalid.ssid[1] = 0x80U;
    if (!network_manager_wifi_config_is_valid(&invalid)) {
        return 9;
    }

    invalid = make_valid_config();
    invalid.ssid_len = NETWORK_MANAGER_WIFI_SSID_MAX_BYTES;
    for (uint8_t index = 0U;
         index < NETWORK_MANAGER_WIFI_SSID_MAX_BYTES;
         ++index) {
        invalid.ssid[index] = 0xFFU;
    }
    if (!network_manager_wifi_config_is_valid(&invalid)) {
        return 10;
    }

    invalid = make_valid_config();
    invalid.password_len = 0U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 11;
    }

    invalid.password_len = 7U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 12;
    }

    invalid.password_len = NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES + 1U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 13;
    }

    invalid = make_valid_config();
    invalid.password_len = 63U;
    for (uint8_t index = 0U; index < invalid.password_len; ++index) {
        invalid.password[index] = 'x';
    }
    if (!network_manager_wifi_config_is_valid(&invalid)) {
        return 14;
    }

    invalid = make_valid_config();
    invalid.password[0] = 0x00U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 15;
    }

    invalid.password[0] = 0x1FU;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 16;
    }

    invalid.password[0] = 0x7FU;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 17;
    }

    for (uint8_t index = 0U; index < invalid.password_len; ++index) {
        invalid.password[index] = 0x20U;
    }
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 18;
    }

    invalid.password[0] = 0x7EU;
    if (!network_manager_wifi_config_is_valid(&invalid)) {
        return 19;
    }

    invalid = make_valid_config();
    invalid.password_len = NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES;
    for (uint8_t index = 0U;
         index < NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES;
         ++index) {
        static const uint8_t hex_digits[] = {'0', '9', 'a', 'f', 'A', 'F'};
        invalid.password[index] = hex_digits[index % sizeof(hex_digits)];
    }
    if (!network_manager_wifi_config_is_valid(&invalid)) {
        return 20;
    }

    invalid.password[0] = 'g';
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 21;
    }

    invalid.password[0] = 0x20U;
    if (network_manager_wifi_config_is_valid(&invalid)) {
        return 22;
    }

    network_manager_wifi_config_t source = make_valid_config();
    network_manager_wifi_config_t destination;
    for (uint8_t index = source.ssid_len;
         index < NETWORK_MANAGER_WIFI_SSID_MAX_BYTES;
         ++index) {
        source.ssid[index] = 0xA5U;
    }
    for (uint8_t index = source.password_len;
         index < NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES;
         ++index) {
        source.password[index] = 0xA5U;
    }
    for (uint8_t index = 0U;
         index < NETWORK_MANAGER_WIFI_SSID_MAX_BYTES;
         ++index) {
        destination.ssid[index] = 0x5AU;
    }
    for (uint8_t index = 0U;
         index < NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES;
         ++index) {
        destination.password[index] = 0x5AU;
    }

    if (!network_manager_wifi_config_copy(&destination, &source)) {
        return 23;
    }
    if (destination.ssid_len != source.ssid_len ||
        destination.password_len != source.password_len) {
        return 24;
    }
    for (uint8_t index = 0U; index < source.ssid_len; ++index) {
        if (destination.ssid[index] != source.ssid[index]) {
            return 25;
        }
    }
    for (uint8_t index = source.ssid_len;
         index < NETWORK_MANAGER_WIFI_SSID_MAX_BYTES;
         ++index) {
        if (0U != destination.ssid[index]) {
            return 26;
        }
    }
    for (uint8_t index = 0U; index < source.password_len; ++index) {
        if (destination.password[index] != source.password[index]) {
            return 27;
        }
    }
    for (uint8_t index = source.password_len;
         index < NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES;
         ++index) {
        if (0U != destination.password[index]) {
            return 28;
        }
    }

    const network_manager_wifi_config_t before_invalid_copy = destination;
    source.password_len = 7U;
    if (network_manager_wifi_config_copy(&destination, &source)) {
        return 29;
    }
    if (!configs_equal(&destination, &before_invalid_copy)) {
        return 30;
    }

    source = make_valid_config();
    if (network_manager_wifi_config_copy(
            (network_manager_wifi_config_t *)0, &source)) {
        return 31;
    }
    if (network_manager_wifi_config_copy(
            &destination, (const network_manager_wifi_config_t *)0)) {
        return 32;
    }
    return 0;
}
