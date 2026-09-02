#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_CAPTURE_POOL_COUNT 3

typedef enum {
    RC_CAPTURE_FREE = 0,
    RC_CAPTURE_WRITING,
    RC_CAPTURE_READY,
    RC_CAPTURE_READING,
} rc_capture_slot_state_t;

typedef struct {
    rc_capture_slot_state_t slots[RC_CAPTURE_POOL_COUNT];
    int latest_ready;
} rc_capture_pool_t;

void rc_capture_pool_init(rc_capture_pool_t *pool);
int rc_capture_pool_select_write(rc_capture_pool_t *pool);
bool rc_capture_pool_finish_write(rc_capture_pool_t *pool, int slot);
bool rc_capture_pool_abort_write(rc_capture_pool_t *pool, int slot);
int rc_capture_pool_acquire_latest(rc_capture_pool_t *pool);
bool rc_capture_pool_release_read(rc_capture_pool_t *pool, int slot);

#ifdef __cplusplus
}
#endif
