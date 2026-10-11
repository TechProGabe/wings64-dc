/* ultra_data.c — libultra data symbols the game references (compiled with the decomp flags).
 * Values per Birdman64 stubs.rs and the N64 boot state; the VI table is filled by the VI shim in M2.
 */
#include <ultra64.h>

u32 osMemSize = 0x400000;      /* 4 MB: PW64 runs without the Expansion Pak */
s32 osTvType = OS_TV_NTSC;     /* the US release; the DC outputs NTSC 480i/480p */
OSViMode osViModeTable[56];    /* M2: unused by the PVR path; only indexed for osViSetMode */

/* RSP microcode images: the game only takes their addresses/sizes for task headers, which the
 * Dreamcast never sends to an RSP (display lists are interpreted on the SH-4, M2). */
long long int rspbootTextStart[1], rspbootTextEnd[1];
long long int gspFast3DTextStart[1], gspFast3DTextEnd[1];
long long int gspFast3DDataStart[1], gspFast3DDataEnd[1];
long long int aspMainTextStart[1], aspMainTextEnd[1];
long long int aspMainDataStart[1], aspMainDataEnd[1];
