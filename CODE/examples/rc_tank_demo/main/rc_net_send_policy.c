#include "rc_net_send_policy.h"

#include <errno.h>

bool rc_net_video_send_should_retry(int send_errno, uint32_t attempt)
{
    if (attempt >= RC_NET_VIDEO_SEND_RETRY_LIMIT) {
        return false;
    }
    return (ENOMEM == send_errno) || (EAGAIN == send_errno) || (EWOULDBLOCK == send_errno);
}

