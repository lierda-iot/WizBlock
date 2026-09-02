#pragma once

#include "demo_network.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

demo_net_state_t lte_net_state_view_map(
    const network_manager_snapshot_t *snapshot,
    bool no_network_timeout,
    demo_net_detail_t *detail);

bool lte_net_state_view_is_immediate(demo_net_state_t state);

#ifdef __cplusplus
}
#endif
