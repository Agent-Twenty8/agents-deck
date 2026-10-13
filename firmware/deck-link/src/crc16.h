/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor.
 * Check value: "123456789" -> 0x29B1. */
#ifndef DECK_CRC16_H
#define DECK_CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DECK_CRC16_INIT 0xFFFFu

/* Continues a CRC over more bytes; start with DECK_CRC16_INIT. */
uint16_t deck_crc16_update(uint16_t crc, const uint8_t *data, size_t len);

/* The CRC of one buffer. */
uint16_t deck_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
