#include "cobs.h"

size_t deck_cobs_encode(const uint8_t *src, size_t len, uint8_t *dst)
{
	size_t out = 1, code_at = 0;
	uint8_t code = 1;
	for (size_t i = 0; i < len; i++) {
		uint8_t c = src[i]; /* read before writing: src and dst may overlap */
		if (c == 0) {
			dst[code_at] = code;
			code_at = out++;
			code = 1;
			continue;
		}
		dst[out++] = c;
		if (++code == 0xFF) {
			dst[code_at] = code;
			code_at = out++;
			code = 1;
		}
	}
	dst[code_at] = code;
	return out;
}

bool deck_cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t *out_len)
{
	size_t i = 0, out = 0;
	while (i < len) {
		size_t code = src[i];
		if (code == 0 || i + code > len)
			return false;
		for (size_t k = 1; k < code; k++)
			dst[out++] = src[i + k];
		i += code;
		if (code < 0xFF && i < len)
			dst[out++] = 0;
	}
	*out_len = out;
	return true;
}
