#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define HOLO_WIFI_SSID_MAX_BYTES 32U
#define HOLO_WIFI_PASSWORD_MAX_BYTES 64U

typedef struct {
    char ssid[HOLO_WIFI_SSID_MAX_BYTES + 1U];
    char password[HOLO_WIFI_PASSWORD_MAX_BYTES + 1U];
} holocubic_wifi_credentials_t;

bool holocubic_wifi_credentials_set(holocubic_wifi_credentials_t *credentials,
                                    const char *ssid,
                                    const char *password);
bool holocubic_wifi_read_line(FILE *file, char *buffer, size_t capacity);
void holocubic_wifi_trim_line(char *line);
