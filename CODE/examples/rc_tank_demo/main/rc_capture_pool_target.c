#include "rc_capture_pool_target.h"

#include <stddef.h>

rc_capture_target_t rc_capture_pool_select_target(rc_capture_pool_t *pool,
                                                  bool backup_available,
                                                  int *out_slot)
{
    if (NULL == pool || NULL == out_slot) {
        return RC_CAPTURE_TARGET_NONE;
    }

    *out_slot = -1;
    const int slot = rc_capture_pool_select_write(pool);
    if (slot >= 0) {
        *out_slot = slot;
        return RC_CAPTURE_TARGET_SLOT;
    }
    return backup_available ? RC_CAPTURE_TARGET_BACKUP : RC_CAPTURE_TARGET_NONE;
}
