#pragma once

#include <stdbool.h>

#include "rc_capture_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RC_CAPTURE_TARGET_NONE = 0,
    RC_CAPTURE_TARGET_SLOT,
    RC_CAPTURE_TARGET_BACKUP,
} rc_capture_target_t;

rc_capture_target_t rc_capture_pool_select_target(rc_capture_pool_t *pool,
                                                   bool backup_available,
                                                   int *out_slot);

#ifdef __cplusplus
}
#endif
