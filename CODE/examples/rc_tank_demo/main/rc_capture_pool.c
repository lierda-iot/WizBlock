#include "rc_capture_pool.h"

#include <stddef.h>

static bool valid_slot(int slot)
{
    return slot >= 0 && slot < RC_CAPTURE_POOL_COUNT;
}

void rc_capture_pool_init(rc_capture_pool_t *pool)
{
    if (NULL == pool) {
        return;
    }
    for (int i = 0; i < RC_CAPTURE_POOL_COUNT; ++i) {
        pool->slots[i] = RC_CAPTURE_FREE;
    }
    pool->latest_ready = -1;
}

int rc_capture_pool_select_write(rc_capture_pool_t *pool)
{
    if (NULL == pool) {
        return -1;
    }

    for (int i = 0; i < RC_CAPTURE_POOL_COUNT; ++i) {
        if (RC_CAPTURE_FREE == pool->slots[i]) {
            pool->slots[i] = RC_CAPTURE_WRITING;
            return i;
        }
    }

    /* A slow consumer may leave one stale READY slot. Reclaim only that slot. */
    if (valid_slot(pool->latest_ready) &&
        RC_CAPTURE_READY == pool->slots[pool->latest_ready]) {
        const int slot = pool->latest_ready;
        pool->latest_ready = -1;
        pool->slots[slot] = RC_CAPTURE_WRITING;
        return slot;
    }
    return -1;
}

bool rc_capture_pool_finish_write(rc_capture_pool_t *pool, int slot)
{
    if (NULL == pool || !valid_slot(slot) ||
        RC_CAPTURE_WRITING != pool->slots[slot]) {
        return false;
    }

    if (valid_slot(pool->latest_ready) && pool->latest_ready != slot &&
        RC_CAPTURE_READY == pool->slots[pool->latest_ready]) {
        pool->slots[pool->latest_ready] = RC_CAPTURE_FREE;
    }
    pool->slots[slot] = RC_CAPTURE_READY;
    pool->latest_ready = slot;
    return true;
}

bool rc_capture_pool_abort_write(rc_capture_pool_t *pool, int slot)
{
    if (NULL == pool || !valid_slot(slot) ||
        RC_CAPTURE_WRITING != pool->slots[slot]) {
        return false;
    }
    pool->slots[slot] = RC_CAPTURE_FREE;
    return true;
}

int rc_capture_pool_acquire_latest(rc_capture_pool_t *pool)
{
    if (NULL == pool || !valid_slot(pool->latest_ready)) {
        return -1;
    }
    const int slot = pool->latest_ready;
    if (RC_CAPTURE_READY != pool->slots[slot]) {
        pool->latest_ready = -1;
        return -1;
    }
    pool->latest_ready = -1;
    pool->slots[slot] = RC_CAPTURE_READING;
    return slot;
}

bool rc_capture_pool_release_read(rc_capture_pool_t *pool, int slot)
{
    if (NULL == pool || !valid_slot(slot) ||
        RC_CAPTURE_READING != pool->slots[slot]) {
        return false;
    }
    pool->slots[slot] = RC_CAPTURE_FREE;
    return true;
}
