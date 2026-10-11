/* layout_checks.c — compile-time checks of struct layouts that raw ROM copies, PW64_SWAP layouts and
 * display-list strides depend on. No code. Built for SH-4 and for the ILP32 host test build.
 * Based on Birdman64 native/src/pw64_layout_checks.c (MIT).
 */
#include <ultra64.h>
#include <libc/stddef.h>
#include <uv_filesystem.h>
#include <uv_graphics.h>
#include "app/snap.h"
#include <PR/sp.h>
#include <pw64_endian.h>

_Static_assert(sizeof(void *) == 4 && sizeof(long) == 4, "ILP32 like the VR4300");
_Static_assert(sizeof(Gfx) == 8, "Gfx");
_Static_assert(sizeof(Vtx) == 16, "Vtx");
_Static_assert(sizeof(Mtx) == 64, "Mtx");
_Static_assert(sizeof(Mtx4F) == 64, "Mtx4F");
_Static_assert(sizeof(UnkCommStruct) == 0x18, "UnkCommStruct (uv_filesystem.h bitfields, patches/001)");
_Static_assert(sizeof(UnkPartStruct_Unk8) == 0x14, "UnkPartStruct_Unk8");
/* SysV/GCC packs this like IDO (0x18) but LSB-first; patches/003 serialises the IDO byte image. */
_Static_assert(sizeof(Unk8033F050) == 0x18, "Unk8033F050 photo record (patches/003)");
_Static_assert(sizeof(Bitmap) == PW64_N64_BITMAP_SIZE, "Bitmap is the N64 16-byte record on ILP32");
