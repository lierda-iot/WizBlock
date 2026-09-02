#include "ofdm_crc.h"

#define CRC16_INITIAL UINT16_C(0xFFFF)
#define CRC16_POLYNOMIAL UINT16_C(0x1021)
#define CRC32_INITIAL UINT32_C(0xFFFFFFFF)
#define CRC32_POLYNOMIAL UINT32_C(0xEDB88320)

uint16_t ofdm_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = CRC16_INITIAL;

    if (NULL == data && 0U != length) {
        return CRC16_INITIAL;
    }
    for (size_t index = 0U; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8U;
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (0U != (crc & UINT16_C(0x8000)))
                      ? (uint16_t)((crc << 1U) ^ CRC16_POLYNOMIAL)
                      : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

uint32_t ofdm_crc32_iso_hdlc(const uint8_t *data, size_t length)
{
    uint32_t crc = CRC32_INITIAL;

    if (NULL == data && 0U != length) {
        return CRC32_INITIAL ^ UINT32_C(0xFFFFFFFF);
    }
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (0U != (crc & UINT32_C(1)))
                      ? (crc >> 1U) ^ CRC32_POLYNOMIAL
                      : crc >> 1U;
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}
