/* wings64_prelude.h — force-included (-include) before every decomp TU.
 * Keep it tiny: it runs ahead of <ultra64.h>. SH-4 is ILP32 like the VR4300, so unlike Birdman64
 * no pointer-width helpers are needed; only the platform switch and the endian swap API.
 */
#ifndef WINGS64_PRELUDE_H
#define WINGS64_PRELUDE_H

#ifndef WINGS64_DC
#define WINGS64_DC 1
#endif

/* Endianness (PLAN D3). ROM data is big-endian; scalars read through uvMemRead/uvConsumeBytes are
 * already converted. Structs copied raw are swapped in place right after the copy:
 *   PW64_SWAP(p, count, T, layout): `count` elements of sizeof(T) at p, each described by `layout`
 *   (`[N]c`, c = b keep / h 2-byte / w 4-byte / d 8-byte swap). Implemented in src/dc/rom/pw64_swap.c,
 *   which aborts if the layout does not add up to sizeof(T). Names match Birdman64 so its patch
 *   hunks apply unchanged. */
void pw64_swap(void *p, unsigned int count, unsigned int stride, const char *layout);
#define PW64_SWAP(p, count, T, layout) \
    pw64_swap((p), (unsigned int)(count), (unsigned int)sizeof(T), (layout))
#define PW64_SWAP16(p, count) pw64_swap((p), (unsigned int)(count), 2, "h")
#define PW64_SWAP32(p, count) pw64_swap((p), (unsigned int)(count), 4, "w")

/* Fatal stop with a message (src/dc/ultra/stubs.c). */
__attribute__((noreturn)) void pw64_fatal(const char *msg, unsigned int value);

#endif
