#pragma once

#include <stddef.h>

#define LTE_NET_DISPLAY_FLUSH_INVALID_ARGUMENT (-1)

typedef int (*lte_net_display_wait_fn_t)(void *context);
typedef void (*lte_net_display_ready_fn_t)(void *context);

static inline int lte_net_display_flush_complete(
    int draw_result,
    lte_net_display_wait_fn_t wait_fn,
    lte_net_display_ready_fn_t ready_fn,
    void *context)
{
    if (NULL == ready_fn) {
        return LTE_NET_DISPLAY_FLUSH_INVALID_ARGUMENT;
    }

    int result = draw_result;
    if (0 == result) {
        if (NULL == wait_fn) {
            result = LTE_NET_DISPLAY_FLUSH_INVALID_ARGUMENT;
        } else {
            result = wait_fn(context);
        }
    }
    ready_fn(context);
    return result;
}
