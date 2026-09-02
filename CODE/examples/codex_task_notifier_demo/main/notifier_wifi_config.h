#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTIFIER_WIFI_SSID_MAX_BYTES      32U
#define NOTIFIER_WIFI_PASSWORD_MIN_BYTES  8U
#define NOTIFIER_WIFI_PASSWORD_MAX_BYTES  63U
#define NOTIFIER_WIFI_SCAN_MAX_NETWORKS   20U

typedef struct {
    char ssid[NOTIFIER_WIFI_SSID_MAX_BYTES + 1U];
    char password[NOTIFIER_WIFI_PASSWORD_MAX_BYTES + 1U];
} notifier_wifi_credentials_t;

typedef struct {
    char ssid[NOTIFIER_WIFI_SSID_MAX_BYTES + 1U];
    int8_t rssi;
} notifier_wifi_scan_network_t;

typedef struct {
    notifier_wifi_scan_network_t networks[NOTIFIER_WIFI_SCAN_MAX_NETWORKS];
    size_t count;
    size_t selected_index;
} notifier_wifi_scan_list_t;

typedef enum {
    NOTIFIER_WIFI_CONFIG_OK = 0,
    NOTIFIER_WIFI_CONFIG_INVALID_ARGUMENT,
    NOTIFIER_WIFI_CONFIG_SSID_REQUIRED,
    NOTIFIER_WIFI_CONFIG_SSID_TOO_LONG,
    NOTIFIER_WIFI_CONFIG_PASSWORD_LENGTH,
    NOTIFIER_WIFI_CONFIG_PASSWORD_CHARACTER,
} notifier_wifi_config_result_t;

notifier_wifi_config_result_t notifier_wifi_config_validate(
    const char *ssid, const char *password);
notifier_wifi_config_result_t notifier_wifi_credentials_set(
    notifier_wifi_credentials_t *credentials, const char *ssid,
    const char *password);
void notifier_wifi_scan_list_init(notifier_wifi_scan_list_t *list);
bool notifier_wifi_scan_list_add(notifier_wifi_scan_list_t *list,
                                 const char *ssid, int8_t rssi);
void notifier_wifi_scan_list_finalize(notifier_wifi_scan_list_t *list,
                                      const char *saved_ssid);
