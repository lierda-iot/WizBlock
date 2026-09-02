#include "holocubic_spi_policy.h"

#include <stddef.h>

void holocubic_spi_policy_init(holocubic_spi_policy_t *policy)
{
    if (NULL != policy) policy->owner = HOLO_SPI_OWNER_NONE;
}

bool holocubic_spi_policy_try_acquire(holocubic_spi_policy_t *policy,
                                      holocubic_spi_owner_t owner)
{
    if (NULL == policy || HOLO_SPI_OWNER_NONE == owner ||
        HOLO_SPI_OWNER_NONE != policy->owner) {
        return false;
    }
    policy->owner = owner;
    return true;
}

bool holocubic_spi_policy_release(holocubic_spi_policy_t *policy,
                                  holocubic_spi_owner_t owner)
{
    if (NULL == policy || HOLO_SPI_OWNER_NONE == owner || owner != policy->owner) {
        return false;
    }
    policy->owner = HOLO_SPI_OWNER_NONE;
    return true;
}
