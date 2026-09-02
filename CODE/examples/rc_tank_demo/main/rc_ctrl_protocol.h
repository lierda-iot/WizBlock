#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_CTRL_MAGIC       0x5243U
#define RC_CTRL_VERSION     1U
#define RC_CTRL_PACKET_SIZE 14U
#define RC_CTRL_ANGLE_MIN_DEG (-180)
#define RC_CTRL_ANGLE_MAX_DEG 180
#define RC_CTRL_MAGNITUDE_MIN 1U
#define RC_CTRL_MAGNITUDE_MAX 100U
#define RC_CTRL_SEQ_NEWER_MAX_DELTA 32767U
#define RC_CTRL_RECEIVER_TIMEOUT_MS 300U

typedef enum {
    RC_CTRL_MODE_STOP = 0,
    RC_CTRL_MODE_DRIVE = 1,
} rc_ctrl_mode_t;

typedef struct {
    rc_ctrl_mode_t mode;
    int16_t angle_deg;
    uint8_t magnitude_pct;
} rc_ctrl_command_t;

typedef struct {
    rc_ctrl_mode_t mode;
    int16_t angle_deg;
    uint8_t magnitude_pct;
    uint16_t seq;
    uint32_t sender_time_ms;
} rc_ctrl_packet_t;

typedef struct {
    bool has_sequence;
    uint16_t last_seq;
    uint32_t last_valid_ms;
} rc_ctrl_receiver_t;

bool rc_ctrl_packet_encode(const rc_ctrl_packet_t *packet,
                           uint8_t *wire,
                           size_t wire_size);
bool rc_ctrl_packet_decode(const uint8_t *wire,
                           size_t wire_size,
                           rc_ctrl_packet_t *packet);
bool rc_ctrl_seq_is_newer(uint16_t new_seq, uint16_t last_seq);
void rc_ctrl_receiver_reset(rc_ctrl_receiver_t *receiver);
bool rc_ctrl_receiver_accept_wire(rc_ctrl_receiver_t *receiver,
                                  const uint8_t *wire,
                                  size_t wire_size,
                                  uint32_t now_ms,
                                  rc_ctrl_command_t *command);
bool rc_ctrl_receiver_is_timed_out(const rc_ctrl_receiver_t *receiver,
                                   uint32_t now_ms);

#ifdef __cplusplus
}
#endif
