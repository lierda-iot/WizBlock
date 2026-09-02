#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RC_NET_VIDEO_SEND_RETRY_LIMIT 3U

bool rc_net_video_send_should_retry(int send_errno, uint32_t attempt);

