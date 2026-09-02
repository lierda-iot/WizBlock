#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t reason;
} wifi_event_sta_disconnected_t;

typedef struct {
    uint32_t status;
} wifi_event_sta_scan_done_t;

typedef struct {
    int unused;
} wifi_init_config_t;

typedef struct {
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
    } sta;
} wifi_config_t;

typedef struct {
    const uint8_t *ssid;
    const uint8_t *bssid;
    uint8_t channel;
    bool show_hidden;
    int scan_type;
} wifi_scan_config_t;

typedef struct {
    uint8_t ssid[33];
    int8_t rssi;
    int authmode;
} wifi_ap_record_t;

#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
#define WIFI_SCAN_TYPE_ACTIVE 0
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_REASON_AUTH_EXPIRE 2
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT 15
#define WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT 16
#define WIFI_REASON_IE_IN_4WAY_DIFFERS 17
#define WIFI_REASON_802_1X_AUTH_FAILED 23
#define WIFI_REASON_BEACON_TIMEOUT 200
#define WIFI_REASON_NO_AP_FOUND 201
#define WIFI_REASON_AUTH_FAIL 202
#define WIFI_REASON_HANDSHAKE_TIMEOUT 204
#define WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY 210
#define WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD 211
#define WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD 212

#define WIFI_STORAGE_RAM 0
#define WIFI_STORAGE_FLASH 1

esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number,
                                       wifi_ap_record_t *records);
esp_err_t esp_wifi_clear_ap_list(void);
