#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t request_id;
    uint32_t snapshot_version;
    float raw_deg;
    float filtered_deg;
    float relative_deg;
    float energy_db;
    uint32_t mic1_rms;
    uint32_t mic2_rms;
    bool valid;
    esp_err_t result;
} companion_doa_result_t;

typedef void (*companion_doa_result_cb_t)(const companion_doa_result_t *result,
                                          void *user_ctx);

typedef struct {
    companion_doa_result_cb_t on_result;
    void *user_ctx;
} companion_doa_config_t;

esp_err_t companion_doa_start(const companion_doa_config_t *config);
esp_err_t companion_doa_request(uint32_t generation, uint32_t wake_seq);
esp_err_t companion_doa_request_ex(uint32_t generation, uint32_t wake_seq,
                                   uint32_t *request_id);
esp_err_t companion_doa_cancel(uint32_t request_id);
bool companion_doa_is_available(void);
