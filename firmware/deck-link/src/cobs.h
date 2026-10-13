/* COBS (Consistent Overhead Byte Stuffing) for Deck Link frames.
 * Same encoding as backend/internal/link/frame.go: the output holds no zero
 * byte; the 0x00 delimiter is not added here. */
#ifndef DECK_COBS_H
#define DECK_COBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The largest encoding of n bytes. */
#define DECK_COBS_MAX(n) ((n) + (n) / 254 + 1)

/* Encodes len bytes of src into dst and returns the encoded length.
 * dst may overlap src when dst + (len / 254 + 2) <= src (in-place encoding
 * of a buffer that starts that far into dst). */
size_t deck_cobs_encode(const uint8_t *src, size_t len, uint8_t *dst);

/* Decodes len bytes of src into dst (dst == src is fine). Stores the decoded
 * length in *out_len and returns true, or returns false on a malformed
 * encoding (a zero code byte or a block running past the end). */
bool deck_cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
