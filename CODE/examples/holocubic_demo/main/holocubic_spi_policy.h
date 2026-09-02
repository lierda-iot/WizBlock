#pragma once

#include <stdbool.h>

typedef enum {
    HOLO_SPI_OWNER_NONE = 0,
    HOLO_SPI_OWNER_DISPLAY,
    HOLO_SPI_OWNER_STORAGE,
} holocubic_spi_owner_t;

typedef struct {
    holocubic_spi_owner_t owner;
} holocubic_spi_policy_t;

void holocubic_spi_policy_init(holocubic_spi_policy_t *policy);
bool holocubic_spi_policy_try_acquire(holocubic_spi_policy_t *policy,
                                      holocubic_spi_owner_t owner);
bool holocubic_spi_policy_release(holocubic_spi_policy_t *policy,
                                  holocubic_spi_owner_t owner);
