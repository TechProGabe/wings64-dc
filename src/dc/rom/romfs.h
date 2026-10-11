/* romfs.h — ROM-in-RAM filesystem image (PLAN D2).
 * disc/wings64.fs is ROM bytes [ROMFS_ROM_START, ROMFS_ROM_END) loaded into one 32-byte-aligned
 * buffer; the cart-DMA choke points (_uvMediaCopy, _uvDMA) will copy from it in M2.
 */
#ifndef WINGS64_ROMFS_H
#define WINGS64_ROMFS_H

#include <stdint.h>

#define ROMFS_ROM_START   0x0DE720u   /* filetable */
#define ROMFS_FILESYS     0x0DF5B0u   /* filesys base: first UV file */
#define ROMFS_FILESYS_END 0x618B6Cu
#define ROMFS_ROM_END     0x6314D0u   /* audio_tbl (not in the image) */

typedef struct {
    int      valid;                   /* fsmeta.txt found and parsed */
    uint32_t size, crc32, index_entries, uv_files;
} romfs_meta_t;

#define ROMFS_MAX_TAGS 32
typedef struct {
    uint32_t index_entries;           /* TABL rows, including empty ones */
    uint32_t uv_files;                /* rows with a non-zero tag */
    uint32_t form_ok;                 /* files whose FORM header + tag matched at the computed offset */
    uint32_t walk_end;                /* ROM address where the walk ended (want ROMFS_FILESYS_END) */
    uint32_t ntags;
    uint32_t tag[ROMFS_MAX_TAGS], count[ROMFS_MAX_TAGS];
} romfs_counts_t;

typedef void (*romfs_progress_fn)(uint32_t done, uint32_t total);

extern uint8_t *romfs_base;
extern uint32_t romfs_size;

int  romfs_meta_read(const char *root, romfs_meta_t *out);
/* Loads <root>/wings64.fs. Returns 0 on success. */
int  romfs_load(const char *root, romfs_progress_fn progress);
uint32_t romfs_crc32(void);
/* Re-implements the decomp's uvMemInitBlockHdr (texture.c:61-121) + uvFileReadBlock
   (filesystem.c:54-101) over the RAM image. Returns 0 on success. */
int  romfs_count(romfs_counts_t *out);
/* ROM address (bit 31 clear) -> pointer into the image; NULL outside the slice. */
const void *romfs_rom_ptr(uint32_t rom_addr);

#endif
