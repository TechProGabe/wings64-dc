/* Shadows decomp include/segment_symbols.h (src/dc/decomp is first on the decomp include path).
 * From Birdman64 (MIT, (c) 2026 Birdman64 contributors) native/include/segment_symbols.h; the ULL
 * suffix dropped (ILP32). On N64 these are linker symbols; here they are constants:
 *  - *_ROM_START/END are ROM offsets (bit 31 clear), which _uvMediaCopy/_uvDMA route to the
 *    ROM-in-RAM image (src/dc/rom/romfs.c, wired in M2).
 *  - kernel/app VRAM/TEXT/BSS are N64 RAM addresses, only printed by uvSysInit except the app overlay
 *    load (system.c:324): app ROM_START==ROM_END and BSS_START==BSS_END make it a no-op because the
 *    app is linked into the ELF. DIVERGES: app overlay load is a no-op; the code is statically linked.
 */
#ifndef SEGMENT_SYMBOLS_H
#define SEGMENT_SYMBOLS_H

#include <PR/ultratypes.h>

#define PW64_SEG(addr) ((u8*)(addr))

#define kernel_ROM_START    PW64_SEG(0x00001050)
#define kernel_ROM_END      PW64_SEG(0x00051E30)
#define kernel_VRAM         PW64_SEG(0x802000A0)
#define kernel_VRAM_END     PW64_SEG(0x802CA900)
#define kernel_TEXT_START   PW64_SEG(0x802000A0)
#define kernel_BSS_START    PW64_SEG(0x80250E80)
#define kernel_BSS_END      PW64_SEG(0x802CA900)

#define app_ROM_START       PW64_SEG(0x00051E30)
#define app_ROM_END         PW64_SEG(0x00051E30) /* really 0xDE720; see above */
#define app_VRAM            PW64_SEG(0x802CA900)
#define app_VRAM_END        PW64_SEG(0x803805E0)
#define app_TEXT_START      PW64_SEG(0x802CA900)
#define app_BSS_START       PW64_SEG(0x803805E0) /* really 0x803571F0; see above */
#define app_BSS_END         PW64_SEG(0x803805E0)

#define filetable_ROM_START PW64_SEG(0x000DE720)
#define filetable_ROM_END   PW64_SEG(0x000DF5B0)
#define filesys_ROM_START   PW64_SEG(0x000DF5B0)
#define filesys_ROM_END     PW64_SEG(0x00618B70)
#define audio_seq_ROM_START PW64_SEG(0x00618B70)
#define audio_seq_ROM_END   PW64_SEG(0x0062D460)
#define audio_ctl_ROM_START PW64_SEG(0x0062D460)
#define audio_ctl_ROM_END   PW64_SEG(0x006314D0)
#define audio_tbl_ROM_START PW64_SEG(0x006314D0)
#define audio_tbl_ROM_END   PW64_SEG(0x00800000)

/* Text/data split is unknown natively and only printed by uvSysInit. */
#define kernel_TEXT_END     kernel_BSS_START
#define kernel_DATA_START   kernel_BSS_START
#define kernel_RODATA_END   kernel_BSS_START
#define app_TEXT_END        app_BSS_START
#define app_DATA_START      app_BSS_START
#define app_RODATA_END      app_BSS_START

#define SEGMENT_VRAM_START(segment) (segment ## _VRAM)
#define SEGMENT_VRAM_END(segment)   (segment ## _VRAM_END)
#define SEGMENT_VRAM_SIZE(segment)  (SEGMENT_VRAM_END(segment) - SEGMENT_VRAM_START(segment))

#define SEGMENT_ROM_START(segment) (segment ## _ROM_START)
#define SEGMENT_ROM_END(segment)   (segment ## _ROM_END)
#define SEGMENT_ROM_SIZE(segment)  (SEGMENT_ROM_END(segment) - SEGMENT_ROM_START(segment))

#define SEGMENT_TEXT_START(segment) (segment ## _TEXT_START)
#define SEGMENT_TEXT_END(segment)   (segment ## _TEXT_END)
#define SEGMENT_TEXT_SIZE(segment)  (SEGMENT_TEXT_END(segment) - SEGMENT_TEXT_START(segment))

#define SEGMENT_DATA_START(segment) (segment ## _DATA_START)
#define SEGMENT_RODATA_END(segment) (segment ## _RODATA_END)
#define SEGMENT_DATA_SIZE(segment)  (SEGMENT_RODATA_END(segment) - SEGMENT_DATA_START(segment))

#define SEGMENT_BSS_START(segment) (segment ## _BSS_START)
#define SEGMENT_BSS_END(segment)   (segment ## _BSS_END)
#define SEGMENT_BSS_SIZE(segment)  (SEGMENT_BSS_END(segment) - SEGMENT_BSS_START(segment))

#endif
