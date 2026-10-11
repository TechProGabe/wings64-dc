/* mio0.h — MIO0 decompressor (Nintendo LZ variant used by PW64 "GZIP" blocks). */
#ifndef WINGS64_MIO0_H
#define WINGS64_MIO0_H

#include <stdint.h>

/* Same signature as the decomp's libultra-era helper (include/kernel/uv_memory.h:67).
   src points at the "MIO0" header; dst must hold the size recorded in the header. */
void mio0_decompress(void *src, uint8_t *dst);

/* Uncompressed size recorded in a MIO0 header (big-endian u32 at +4); 0 if magic is wrong. */
uint32_t mio0_out_size(const void *src);

#endif
