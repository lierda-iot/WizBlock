#include "ofdm_utf8.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    static const uint8_t chinese[] = "你好，OFDM";
    static const uint8_t allowed_controls[] = "line1\nline2\t\r";
    static const uint8_t embedded_nul[] = {'o', 'k', 0, 'x'};
    static const uint8_t forbidden_control[] = {'o', 1, 'k'};
    static const uint8_t forbidden_delete[] = {'o', 0x7F, 'k'};
    static const uint8_t invalid_continuation[] = {0x80};
    static const uint8_t overlong[] = {0xC0, 0x80};
    static const uint8_t surrogate[] = {0xED, 0xA0, 0x80};
    static const uint8_t too_high[] = {0xF4, 0x90, 0x80, 0x80};
    static uint8_t too_long[OFDM_MESSAGE_MAX_BYTES + 1U];

    assert(OFDM_MESSAGE_OK ==
           ofdm_message_validate(chinese, sizeof(chinese) - 1U));
    assert(OFDM_MESSAGE_OK == ofdm_message_validate(
                                  allowed_controls,
                                  sizeof(allowed_controls) - 1U));
    assert(OFDM_MESSAGE_EMPTY == ofdm_message_validate(NULL, 0U));
    assert(OFDM_MESSAGE_NULL == ofdm_message_validate(NULL, 1U));
    assert(OFDM_MESSAGE_EMBEDDED_NUL == ofdm_message_validate(
                                              embedded_nul,
                                              sizeof(embedded_nul)));
    assert(OFDM_MESSAGE_CONTROL == ofdm_message_validate(
                                         forbidden_control,
                                         sizeof(forbidden_control)));
    assert(OFDM_MESSAGE_CONTROL == ofdm_message_validate(
                                         forbidden_delete,
                                         sizeof(forbidden_delete)));
    assert(OFDM_MESSAGE_INVALID_UTF8 == ofdm_message_validate(
                                             invalid_continuation,
                                             sizeof(invalid_continuation)));
    assert(OFDM_MESSAGE_INVALID_UTF8 == ofdm_message_validate(
                                             overlong,
                                             sizeof(overlong)));
    assert(OFDM_MESSAGE_INVALID_UTF8 == ofdm_message_validate(
                                             surrogate,
                                             sizeof(surrogate)));
    assert(OFDM_MESSAGE_INVALID_UTF8 == ofdm_message_validate(
                                             too_high,
                                             sizeof(too_high)));
    assert(OFDM_MESSAGE_TOO_LONG == ofdm_message_validate(
                                         too_long,
                                         sizeof(too_long)));

    puts("ofdm_utf8_test: PASS");
    return 0;
}
