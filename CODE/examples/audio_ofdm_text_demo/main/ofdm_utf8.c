#include "ofdm_utf8.h"

#include <stdbool.h>

#define ASCII_CONTROL_LIMIT 0x20U
#define ASCII_DELETE_CODE_POINT 0x7FU

static bool is_continuation_byte(uint8_t value)
{
    return 0x80U == (value & 0xC0U);
}

static bool is_allowed_control(uint32_t code_point)
{
    return '\n' == code_point || '\r' == code_point || '\t' == code_point;
}

static bool decode_code_point(const uint8_t *data,
                              size_t length,
                              size_t *offset,
                              uint32_t *code_point)
{
    const size_t start = *offset;
    const uint8_t first = data[start];
    uint32_t value = 0U;
    size_t continuation_count = 0U;
    uint32_t minimum = 0U;

    if (first <= 0x7FU) {
        value = first;
    } else if (first >= 0xC2U && first <= 0xDFU) {
        value = first & 0x1FU;
        continuation_count = 1U;
        minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        value = first & 0x0FU;
        continuation_count = 2U;
        minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        value = first & 0x07U;
        continuation_count = 3U;
        minimum = 0x10000U;
    } else {
        return false;
    }

    if (length - start <= continuation_count) {
        return false;
    }
    for (size_t index = 1U; index <= continuation_count; ++index) {
        const uint8_t next = data[start + index];
        if (!is_continuation_byte(next)) {
            return false;
        }
        value = (value << 6U) | (uint32_t)(next & 0x3FU);
    }
    if (value < minimum || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
        return false;
    }
    *offset = start + continuation_count + 1U;
    *code_point = value;
    return true;
}

ofdm_message_validation_t ofdm_message_validate(const uint8_t *data,
                                                size_t length)
{
    if (NULL == data && 0U == length) {
        return OFDM_MESSAGE_EMPTY;
    }
    if (NULL == data) {
        return OFDM_MESSAGE_NULL;
    }
    if (0U == length) {
        return OFDM_MESSAGE_EMPTY;
    }
    if (OFDM_MESSAGE_MAX_BYTES < length) {
        return OFDM_MESSAGE_TOO_LONG;
    }

    size_t offset = 0U;
    while (offset < length) {
        uint32_t code_point = 0U;
        if (!decode_code_point(data, length, &offset, &code_point)) {
            return OFDM_MESSAGE_INVALID_UTF8;
        }
        if (0U == code_point) {
            return OFDM_MESSAGE_EMBEDDED_NUL;
        }
        if ((code_point < ASCII_CONTROL_LIMIT ||
             ASCII_DELETE_CODE_POINT == code_point) &&
            !is_allowed_control(code_point)) {
            return OFDM_MESSAGE_CONTROL;
        }
    }
    return OFDM_MESSAGE_OK;
}
