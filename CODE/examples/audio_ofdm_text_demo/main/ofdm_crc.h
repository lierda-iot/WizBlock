#ifndef OFDM_CRC_H
#define OFDM_CRC_H

#include <stddef.h>
#include <stdint.h>

uint16_t ofdm_crc16_ccitt_false(const uint8_t *data, size_t length);
uint32_t ofdm_crc32_iso_hdlc(const uint8_t *data, size_t length);

#endif
