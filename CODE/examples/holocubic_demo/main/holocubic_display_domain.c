#include "holocubic_display_domain.h"

#include "holocubic_spi_policy.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_spi_mutex;
static holocubic_spi_policy_t s_spi_policy;

static esp_err_t lock_spi_domain(holocubic_spi_owner_t owner, int timeout_ms)
{
    if (NULL == s_spi_mutex) return ESP_ERR_INVALID_STATE;
    if (0 > timeout_ms) return ESP_ERR_INVALID_ARG;
    if (pdTRUE != xSemaphoreTake(s_spi_mutex, pdMS_TO_TICKS(timeout_ms))) {
        return ESP_ERR_TIMEOUT;
    }
    if (!holocubic_spi_policy_try_acquire(&s_spi_policy, owner)) {
        (void)xSemaphoreGive(s_spi_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void unlock_spi_domain(holocubic_spi_owner_t owner)
{
    if (NULL != s_spi_mutex && holocubic_spi_policy_release(&s_spi_policy, owner)) {
        (void)xSemaphoreGive(s_spi_mutex);
    }
}

esp_err_t holocubic_display_domain_init(void)
{
    if (NULL != s_spi_mutex) return ESP_OK;
    s_spi_mutex = xSemaphoreCreateMutex();
    if (NULL == s_spi_mutex) return ESP_ERR_NO_MEM;
    holocubic_spi_policy_init(&s_spi_policy);
    return ESP_OK;
}

esp_err_t holocubic_display_domain_lock(int timeout_ms)
{
    return lock_spi_domain(HOLO_SPI_OWNER_DISPLAY, timeout_ms);
}

void holocubic_display_domain_unlock(void)
{
    unlock_spi_domain(HOLO_SPI_OWNER_DISPLAY);
}

esp_err_t holocubic_storage_domain_lock(int timeout_ms)
{
    return lock_spi_domain(HOLO_SPI_OWNER_STORAGE, timeout_ms);
}

void holocubic_storage_domain_unlock(void)
{
    unlock_spi_domain(HOLO_SPI_OWNER_STORAGE);
}
