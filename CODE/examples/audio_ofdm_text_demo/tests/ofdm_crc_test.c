#include "ofdm_crc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const uint8_t check[] = "123456789";

    assert(UINT16_C(0x29B1) ==
           ofdm_crc16_ccitt_false(check, strlen((const char *)check)));
    assert(UINT32_C(0xCBF43926) ==
           ofdm_crc32_iso_hdlc(check, strlen((const char *)check)));
    assert(UINT16_C(0xFFFF) == ofdm_crc16_ccitt_false(NULL, 0U));
    assert(UINT32_C(0x00000000) == ofdm_crc32_iso_hdlc(NULL, 0U));

    puts("ofdm_crc_test: PASS");
    return 0;
}
