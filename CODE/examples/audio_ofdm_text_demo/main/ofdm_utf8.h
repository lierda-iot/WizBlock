#ifndef OFDM_UTF8_H
#define OFDM_UTF8_H

#include <stddef.h>
#include <stdint.h>

#define OFDM_MESSAGE_MAX_BYTES 1024U

typedef enum {
    OFDM_MESSAGE_OK = 0,
    OFDM_MESSAGE_NULL,
    OFDM_MESSAGE_EMPTY,
    OFDM_MESSAGE_TOO_LONG,
    OFDM_MESSAGE_INVALID_UTF8,
    OFDM_MESSAGE_EMBEDDED_NUL,
    OFDM_MESSAGE_CONTROL,
} ofdm_message_validation_t;

ofdm_message_validation_t ofdm_message_validate(const uint8_t *data,
                                                size_t length);

#endif
