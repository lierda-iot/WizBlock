#include "network_manager_storage.h"

static network_manager_wifi_config_t make_config(void)
{
    network_manager_wifi_config_t config = {0};

    config.ssid[0] = 'a';
    config.ssid[1] = 'p';
    config.ssid_len = 2U;
    for (uint8_t index = 0U; index < 8U; ++index) {
        config.password[index] = 'x';
    }
    config.password_len = 8U;
    return config;
}

typedef enum {
    FAKE_CALL_WRITE_BLOB = 1,
    FAKE_CALL_COMMIT = 2,
    FAKE_CALL_READ_BLOB = 3,
    FAKE_CALL_WRITE_SELECTOR = 4,
} fake_call_t;

typedef struct {
    fake_call_t calls[8];
    size_t call_count;
    network_manager_storage_blob_t stored_blob;
    network_manager_storage_slot_t stored_slot;
    network_manager_storage_slot_t selector;
    bool corrupt_readback;
} fake_storage_t;

static bool fake_write_blob(void *context,
                            network_manager_storage_slot_t slot,
                            const network_manager_storage_blob_t *blob)
{
    fake_storage_t *fake = (fake_storage_t *)context;
    fake->calls[fake->call_count++] = FAKE_CALL_WRITE_BLOB;
    fake->stored_slot = slot;
    fake->stored_blob = *blob;
    return true;
}

static bool fake_commit(void *context)
{
    fake_storage_t *fake = (fake_storage_t *)context;
    fake->calls[fake->call_count++] = FAKE_CALL_COMMIT;
    return true;
}

static bool fake_read_blob(void *context,
                           network_manager_storage_slot_t slot,
                           network_manager_storage_blob_t *blob)
{
    fake_storage_t *fake = (fake_storage_t *)context;
    fake->calls[fake->call_count++] = FAKE_CALL_READ_BLOB;
    if (slot != fake->stored_slot) {
        return false;
    }
    *blob = fake->stored_blob;
    if (fake->corrupt_readback) {
        blob->ssid[0] ^= 1U;
    }
    return true;
}

static bool fake_write_selector(void *context,
                                network_manager_storage_slot_t selector)
{
    fake_storage_t *fake = (fake_storage_t *)context;
    fake->calls[fake->call_count++] = FAKE_CALL_WRITE_SELECTOR;
    fake->selector = selector;
    return true;
}

int main(void)
{
    const network_manager_wifi_config_t config = make_config();
    network_manager_storage_blob_t blob;

    if (!network_manager_storage_blob_encode(&config, 1U, &blob)) {
        return 1;
    }
    if (!network_manager_storage_blob_is_valid(&blob) ||
        NETWORK_MANAGER_STORAGE_MAGIC != blob.magic ||
        NETWORK_MANAGER_STORAGE_SCHEMA_VERSION != blob.schema_version ||
        1U != blob.generation) {
        return 2;
    }
    for (uint8_t index = config.ssid_len;
         index < NETWORK_MANAGER_WIFI_SSID_MAX_BYTES;
         ++index) {
        if (0U != blob.ssid[index]) {
            return 3;
        }
    }
    for (uint8_t index = config.password_len;
         index < NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES;
         ++index) {
        if (0U != blob.password[index]) {
            return 4;
        }
    }

    blob.ssid[0] ^= 1U;
    if (network_manager_storage_blob_is_valid(&blob)) {
        return 5;
    }
    blob.ssid[0] ^= 1U;
    ++blob.schema_version;
    if (network_manager_storage_blob_is_valid(&blob)) {
        return 6;
    }

    network_manager_storage_image_t image = {0};
    network_manager_wifi_config_t second_config = make_config();
    network_manager_wifi_config_t loaded;
    uint32_t generation = 0U;
    network_manager_storage_slot_t loaded_slot =
        NETWORK_MANAGER_STORAGE_SLOT_NONE;

    second_config.ssid[0] = 'b';
    image.slot_present[NETWORK_MANAGER_STORAGE_SLOT_A] = true;
    image.slot_present[NETWORK_MANAGER_STORAGE_SLOT_B] = true;
    image.selector_present = true;
    image.selector = NETWORK_MANAGER_STORAGE_SLOT_A;
    if (!network_manager_storage_blob_encode(
            &config,
            1U,
            &image.slots[NETWORK_MANAGER_STORAGE_SLOT_A]) ||
        !network_manager_storage_blob_encode(
            &second_config,
            2U,
            &image.slots[NETWORK_MANAGER_STORAGE_SLOT_B])) {
        return 7;
    }
    if (NETWORK_MANAGER_STORAGE_LOAD_OK !=
        network_manager_storage_model_load(
            &image, &loaded, &generation, &loaded_slot)) {
        return 8;
    }
    if (NETWORK_MANAGER_STORAGE_SLOT_A != loaded_slot ||
        1U != generation || 'a' != loaded.ssid[0]) {
        return 9;
    }

    image.slots[NETWORK_MANAGER_STORAGE_SLOT_A].ssid[0] ^= 1U;
    if (NETWORK_MANAGER_STORAGE_LOAD_OK !=
        network_manager_storage_model_load(
            &image, &loaded, &generation, &loaded_slot)) {
        return 10;
    }
    if (NETWORK_MANAGER_STORAGE_SLOT_B != loaded_slot ||
        2U != generation || 'b' != loaded.ssid[0]) {
        return 11;
    }

    image.selector_present = false;
    if (!network_manager_storage_blob_encode(
            &config,
            1U,
            &image.slots[NETWORK_MANAGER_STORAGE_SLOT_A])) {
        return 12;
    }
    if (NETWORK_MANAGER_STORAGE_LOAD_OK !=
        network_manager_storage_model_load(
            &image, &loaded, &generation, &loaded_slot)) {
        return 13;
    }
    if (NETWORK_MANAGER_STORAGE_SLOT_B != loaded_slot ||
        2U != generation) {
        return 14;
    }

    network_manager_storage_image_t empty = {0};
    if (NETWORK_MANAGER_STORAGE_LOAD_NOT_FOUND !=
        network_manager_storage_model_load(
            &empty, &loaded, &generation, &loaded_slot)) {
        return 15;
    }
    empty.slot_present[NETWORK_MANAGER_STORAGE_SLOT_A] = true;
    if (NETWORK_MANAGER_STORAGE_LOAD_CORRUPT !=
        network_manager_storage_model_load(
            &empty, &loaded, &generation, &loaded_slot)) {
        return 16;
    }

    image.selector_present = false;
    if (!network_manager_storage_blob_encode(
            &config,
            UINT32_MAX,
            &image.slots[NETWORK_MANAGER_STORAGE_SLOT_A]) ||
        !network_manager_storage_blob_encode(
            &second_config,
            1U,
            &image.slots[NETWORK_MANAGER_STORAGE_SLOT_B])) {
        return 17;
    }
    if (NETWORK_MANAGER_STORAGE_LOAD_OK !=
        network_manager_storage_model_load(
            &image, &loaded, &generation, &loaded_slot) ||
        NETWORK_MANAGER_STORAGE_SLOT_B != loaded_slot ||
        1U != generation) {
        return 18;
    }

    image = (network_manager_storage_image_t){0};
    image.slot_present[NETWORK_MANAGER_STORAGE_SLOT_A] = true;
    image.selector_present = true;
    image.selector = NETWORK_MANAGER_STORAGE_SLOT_A;
    if (!network_manager_storage_blob_encode(
            &config,
            1U,
            &image.slots[NETWORK_MANAGER_STORAGE_SLOT_A])) {
        return 19;
    }
    network_manager_storage_save_plan_t plan;
    if (!network_manager_storage_model_prepare_save(
            &image, &second_config, &plan)) {
        return 20;
    }
    if (NETWORK_MANAGER_STORAGE_SLOT_B != plan.target_slot ||
        NETWORK_MANAGER_STORAGE_SLOT_B != plan.selector ||
        2U != plan.blob.generation ||
        !network_manager_storage_blob_is_valid(&plan.blob) ||
        image.slot_present[NETWORK_MANAGER_STORAGE_SLOT_B]) {
        return 21;
    }

    image.slots[plan.target_slot] = plan.blob;
    image.slot_present[plan.target_slot] = true;
    if (NETWORK_MANAGER_STORAGE_LOAD_OK !=
        network_manager_storage_model_load(
            &image, &loaded, &generation, &loaded_slot) ||
        NETWORK_MANAGER_STORAGE_SLOT_A != loaded_slot ||
        'a' != loaded.ssid[0]) {
        return 22;
    }
    image.selector = plan.selector;
    if (NETWORK_MANAGER_STORAGE_LOAD_OK !=
        network_manager_storage_model_load(
            &image, &loaded, &generation, &loaded_slot) ||
        NETWORK_MANAGER_STORAGE_SLOT_B != loaded_slot ||
        'b' != loaded.ssid[0]) {
        return 23;
    }

    image.selector = NETWORK_MANAGER_STORAGE_SLOT_A;
    fake_storage_t fake = {0};
    const network_manager_storage_ops_t ops = {
        .context = &fake,
        .write_blob = fake_write_blob,
        .commit = fake_commit,
        .read_blob = fake_read_blob,
        .write_selector = fake_write_selector,
    };
    if (NETWORK_MANAGER_STORAGE_SAVE_OK !=
        network_manager_storage_execute_save(&image, &second_config, &ops)) {
        return 24;
    }
    if (5U != fake.call_count ||
        FAKE_CALL_WRITE_BLOB != fake.calls[0] ||
        FAKE_CALL_COMMIT != fake.calls[1] ||
        FAKE_CALL_READ_BLOB != fake.calls[2] ||
        FAKE_CALL_WRITE_SELECTOR != fake.calls[3] ||
        FAKE_CALL_COMMIT != fake.calls[4] ||
        NETWORK_MANAGER_STORAGE_SLOT_B != fake.stored_slot ||
        NETWORK_MANAGER_STORAGE_SLOT_B != fake.selector) {
        return 25;
    }

    fake = (fake_storage_t){
        .corrupt_readback = true,
        .selector = NETWORK_MANAGER_STORAGE_SLOT_NONE,
    };
    if (NETWORK_MANAGER_STORAGE_SAVE_VERIFY_FAILED !=
        network_manager_storage_execute_save(&image, &second_config, &ops)) {
        return 26;
    }
    if (3U != fake.call_count ||
        NETWORK_MANAGER_STORAGE_SLOT_NONE != fake.selector) {
        return 27;
    }
    return 0;
}
