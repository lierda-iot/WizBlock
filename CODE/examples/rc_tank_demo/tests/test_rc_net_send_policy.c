#include "rc_net_send_policy.h"

#include <errno.h>

static int failures;

static void expect_true(bool condition)
{
    if (!condition) {
        ++failures;
    }
}

int main(void)
{
    expect_true(rc_net_video_send_should_retry(ENOMEM, 0U));
    expect_true(rc_net_video_send_should_retry(EAGAIN, 2U));
    expect_true(!rc_net_video_send_should_retry(ENOMEM, RC_NET_VIDEO_SEND_RETRY_LIMIT));
    expect_true(!rc_net_video_send_should_retry(ECONNRESET, 0U));
    return failures;
}

