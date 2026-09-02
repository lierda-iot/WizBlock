#include "xiaozhi_agent_listen_mode_policy.h"

const char *xiaozhi_agent_listen_start_fields(bool client_manages_stop)
{
    return client_manages_stop ?
        "\"state\":\"start\",\"mode\":\"manual\"" :
        "\"state\":\"start\",\"mode\":\"auto\"";
}
