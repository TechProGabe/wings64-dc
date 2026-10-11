/* pw64_endian.h — ROM-layout loaders for structs that cannot be swapped in place.
 * Names follow Birdman64 (MIT) so its patch hunks apply unchanged (patches/015, 016).
 */
#ifndef PW64_ENDIAN_H
#define PW64_ENDIAN_H

#include <ultra64.h>
#include <PR/libaudio.h>

/* ILP32: a 32-bit N64-style address and a pointer are the same width. */
#define PW64_PTR(a) ((void *)(u32)(a))
#define PW64_U32(p) ((u32)(p))

/* src/dc/rom/pw64_arena.c: zeroed, never-freed bump allocation for deserialised ROM structs. */
void *pw64_arena_alloc(u32 size, u32 align);

/* src/dc/rom/pw64_audio_load.c */
ALBankFile *pw64_alBnkfLoad(void *src, u32 size, u8 *table);
ALSeqFile *pw64_alSeqFileLoad(void *src, ALHeap *heap);

/* src/dc/rom/pw64_rom_structs.c: N64 Bitmap records (16 bytes, big-endian) -> host. */
struct bitmap;
#define PW64_N64_BITMAP_SIZE 16
void pw64_load_bitmaps(struct bitmap *dst, const void *src, s32 count);

#endif
