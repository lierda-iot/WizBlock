#pragma once

/** Platform-independent two-slot Wi-Fi configuration transaction model. */

#include "network_manager.h"

#include <stdbool.h>
#include <stdint.h>

#define NETWORK_MANAGER_STORAGE_MAGIC           0x4E4D5746U
#define NETWORK_MANAGER_STORAGE_SCHEMA_VERSION  1U

typedef struct {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t reserved;
    uint32_t generation;
    uint8_t ssid_len;
    uint8_t password_len;
    uint8_t ssid[NETWORK_MANAGER_WIFI_SSID_MAX_BYTES];
    uint8_t password[NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES];
    uint32_t crc32;
} network_manager_storage_blob_t;

typedef enum {
    NETWORK_MANAGER_STORAGE_SLOT_A = 0,
    NETWORK_MANAGER_STORAGE_SLOT_B = 1,
    NETWORK_MANAGER_STORAGE_SLOT_NONE = 2,
} network_manager_storage_slot_t;

typedef enum {
    NETWORK_MANAGER_STORAGE_LOAD_OK = 0,
    NETWORK_MANAGER_STORAGE_LOAD_NOT_FOUND = 1,
    NETWORK_MANAGER_STORAGE_LOAD_CORRUPT = 2,
    NETWORK_MANAGER_STORAGE_LOAD_INVALID_ARGUMENT = 3,
} network_manager_storage_load_result_t;

typedef struct {
    network_manager_storage_blob_t slots[2];
    bool slot_present[2];
    bool selector_present;
    network_manager_storage_slot_t selector;
} network_manager_storage_image_t;

typedef struct {
    network_manager_storage_slot_t target_slot;
    network_manager_storage_slot_t selector;
    network_manager_storage_blob_t blob;
} network_manager_storage_save_plan_t;

typedef enum {
    NETWORK_MANAGER_STORAGE_SAVE_OK = 0,
    NETWORK_MANAGER_STORAGE_SAVE_INVALID_ARGUMENT = 1,
    NETWORK_MANAGER_STORAGE_SAVE_PREPARE_FAILED = 2,
    NETWORK_MANAGER_STORAGE_SAVE_WRITE_FAILED = 3,
    NETWORK_MANAGER_STORAGE_SAVE_SLOT_COMMIT_FAILED = 4,
    NETWORK_MANAGER_STORAGE_SAVE_READBACK_FAILED = 5,
    NETWORK_MANAGER_STORAGE_SAVE_VERIFY_FAILED = 6,
    NETWORK_MANAGER_STORAGE_SAVE_SELECTOR_WRITE_FAILED = 7,
    NETWORK_MANAGER_STORAGE_SAVE_SELECTOR_COMMIT_FAILED = 8,
} network_manager_storage_save_result_t;

typedef struct {
    void *context;
    bool (*write_blob)(void *context,
                       network_manager_storage_slot_t slot,
                       const network_manager_storage_blob_t *blob);
    bool (*commit)(void *context);
    bool (*read_blob)(void *context,
                      network_manager_storage_slot_t slot,
                      network_manager_storage_blob_t *blob);
    bool (*write_selector)(void *context,
                           network_manager_storage_slot_t selector);
} network_manager_storage_ops_t;

/** Encode canonical credentials and generation into a checksummed blob. */
bool network_manager_storage_blob_encode(
    const network_manager_wifi_config_t *config,
    uint32_t generation,
    network_manager_storage_blob_t *blob);
/** Validate schema, generation, credentials, and CRC. */
bool network_manager_storage_blob_is_valid(
    const network_manager_storage_blob_t *blob);
/** Select the authoritative valid slot and decode its credentials. */
network_manager_storage_load_result_t network_manager_storage_model_load(
    const network_manager_storage_image_t *image,
    network_manager_wifi_config_t *config,
    uint32_t *generation,
    network_manager_storage_slot_t *loaded_slot);
/** Choose the inactive target slot and prepare its next-generation blob. */
bool network_manager_storage_model_prepare_save(
    const network_manager_storage_image_t *image,
    const network_manager_wifi_config_t *config,
    network_manager_storage_save_plan_t *plan);
/** Execute write, commit, readback verification, selector write, and commit. */
network_manager_storage_save_result_t network_manager_storage_execute_save(
    const network_manager_storage_image_t *image,
    const network_manager_wifi_config_t *config,
    const network_manager_storage_ops_t *ops);
