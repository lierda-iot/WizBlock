#include "storage_hal.h"

#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include <string.h>

static const char *TAG = "storage_hal";
#define STORAGE_HAL_MOUNT_POINT_MAX 32

static storage_hal_config_t s_config;
static sdmmc_card_t *s_card;
static bool s_mounted;
static char s_mount_point[STORAGE_HAL_MOUNT_POINT_MAX];

esp_err_t storage_hal_init(const storage_hal_config_t *config)
{
    ESP_RETURN_ON_FALSE(NULL != config && NULL != config->mount_point,
                        ESP_ERR_INVALID_ARG, TAG, "invalid config");
    ESP_RETURN_ON_FALSE(config->cs_gpio_num >= 0 && config->max_freq_khz > 0 &&
                            config->max_files > 0 && config->allocation_unit_size > 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid config values");

    if (s_mounted) {
        return ESP_OK;
    }

    size_t mount_point_len = strlen(config->mount_point);
    ESP_RETURN_ON_FALSE(mount_point_len < sizeof(s_mount_point),
                        ESP_ERR_INVALID_ARG, TAG, "mount point too long");

    s_config = *config;
    memcpy(s_mount_point, config->mount_point, mount_point_len + 1U);
    s_config.mount_point = s_mount_point;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = config->spi_host;
    host.max_freq_khz = config->max_freq_khz;

    sdspi_device_config_t device_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    device_config.host_id = config->spi_host;
    device_config.gpio_cs = config->cs_gpio_num;

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = config->format_if_mount_failed,
        .max_files = config->max_files,
        .allocation_unit_size = config->allocation_unit_size,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(s_config.mount_point, &host, &device_config,
                                            &mount_config, &s_card);
    if (ESP_OK != ret) {
        s_card = NULL;
        ESP_LOGE(TAG, "TF mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "TF mounted at %s, capacity=%llu bytes, speed=%d kHz",
             s_config.mount_point,
             (unsigned long long)storage_hal_capacity_bytes(),
             host.max_freq_khz);
    return ESP_OK;
}

esp_err_t storage_hal_deinit(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(s_config.mount_point, s_card);
    if (ESP_OK == ret) {
        s_card = NULL;
        s_mounted = false;
    }
    return ret;
}

esp_err_t storage_hal_probe(void)
{
    return s_mounted ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool storage_hal_is_mounted(void)
{
    return s_mounted;
}

uint64_t storage_hal_capacity_bytes(void)
{
    if (!s_mounted || NULL == s_card) {
        return 0;
    }
    return (uint64_t)s_card->csd.capacity * (uint64_t)s_card->csd.sector_size;
}
