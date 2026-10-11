/* romfs.c — load disc/wings64.fs into RAM and validate it the way the game would. */
#include "romfs.h"
#include "../db.h"
#include "../ultra/mio0.h"

#include <kos.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#define TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define LOAD_CHUNK (128u * 1024u)     /* multiple of 2048: keeps iso9660 reads on the DMA path */

uint8_t *romfs_base;
uint32_t romfs_size;

/* The image stays big-endian (PLAN D3); every multi-byte field is read byte-wise. */
static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

const void *romfs_rom_ptr(uint32_t rom_addr) {
    if (rom_addr < ROMFS_ROM_START || rom_addr >= ROMFS_ROM_START + romfs_size) return NULL;
    return romfs_base + (rom_addr - ROMFS_ROM_START);
}

static uint32_t parse_u32(const char *s) { return (uint32_t)strtoul(s, NULL, 0); }

int romfs_meta_read(const char *root, romfs_meta_t *m) {
    char path[128], buf[1024], *line, *save = NULL;
    file_t f;
    ssize_t n;
    memset(m, 0, sizeof *m);
    snprintf(path, sizeof path, "%s/fsmeta.txt", root);
    f = fs_open(path, O_RDONLY);
    if (f < 0) return -1;
    n = fs_read(f, buf, sizeof buf - 1);
    fs_close(f);
    if (n <= 0) return -1;
    buf[n] = 0;
    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "size=", 5)) m->size = parse_u32(line + 5);
        else if (!strncmp(line, "crc32=", 6)) m->crc32 = parse_u32(line + 6);
        else if (!strncmp(line, "index_entries=", 14)) m->index_entries = parse_u32(line + 14);
        else if (!strncmp(line, "uv_files=", 9)) m->uv_files = parse_u32(line + 9);
    }
    m->valid = m->size != 0;
    return m->valid ? 0 : -1;
}

int romfs_load(const char *root, romfs_progress_fn progress) {
    char path[128];
    file_t f;
    uint32_t total, done = 0;
    snprintf(path, sizeof path, "%s/wings64.fs", root);
    f = fs_open(path, O_RDONLY);
    if (f < 0) {
        db_log("romfs: cannot open %s\n", path);
        return -1;
    }
    total = (uint32_t)fs_total(f);
    if (total != ROMFS_ROM_END - ROMFS_ROM_START) {
        db_log("romfs: %s is %lu bytes, want %lu\n", path, (unsigned long)total,
               (unsigned long)(ROMFS_ROM_END - ROMFS_ROM_START));
        fs_close(f);
        return -1;
    }
    /* 32-byte aligned for GD-ROM DMA and later shz_memcpy32 fast paths. */
    romfs_base = memalign(32, (total + 31u) & ~31u);
    if (!romfs_base) {
        db_log("romfs: out of memory for %lu bytes\n", (unsigned long)total);
        fs_close(f);
        return -1;
    }
    while (done < total) {
        uint32_t want = total - done < LOAD_CHUNK ? total - done : LOAD_CHUNK;
        ssize_t got = fs_read(f, romfs_base + done, want);
        if (got <= 0) {
            db_log("romfs: read failed at %lu\n", (unsigned long)done);
            fs_close(f);
            return -1;
        }
        done += (uint32_t)got;
        if (progress) progress(done, total);
    }
    fs_close(f);
    romfs_size = total;
    return 0;
}

uint32_t romfs_crc32(void) {
    static uint32_t table[256];
    uint32_t crc = 0xFFFFFFFFu;
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    for (uint32_t i = 0; i < romfs_size; i++) crc = table[(crc ^ romfs_base[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void count_tag(romfs_counts_t *c, uint32_t tag) {
    for (uint32_t i = 0; i < c->ntags; i++) {
        if (c->tag[i] == tag) { c->count[i]++; return; }
    }
    if (c->ntags < ROMFS_MAX_TAGS) {
        c->tag[c->ntags] = tag;
        c->count[c->ntags++] = 1;
    }
}

int romfs_count(romfs_counts_t *c) {
    const uint8_t *ft = romfs_rom_ptr(ROMFS_ROM_START);
    uint32_t form_len, off, fs_off;
    memset(c, 0, sizeof *c);
    if (!ft || be32(ft) != TAG('F', 'O', 'R', 'M') || be32(ft + 8) != TAG('U', 'V', 'R', 'M')) {
        db_log("romfs: filetable is not FORM/UVRM\n");
        return -1;
    }
    /* uvFileReadHeader: length = FORM size + 8; blocks start after FORM/len/tag. */
    form_len = be32(ft + 4) + 8;
    fs_off = ROMFS_FILESYS;
    for (off = 12; off < form_len;) {
        uint32_t tag = be32(ft + off), size = be32(ft + off + 4);
        const uint8_t *data = ft + off + 8;
        uint8_t *tabl = NULL;
        uint32_t tabl_size = size;
        if (tag == TAG('G', 'Z', 'I', 'P')) {
            /* uvFileReadBlock: inner tag at +0, raw size at +4, MIO0 stream at +8. */
            uint32_t raw = be32(data + 4);
            tag = be32(data);
            if (mio0_out_size(data + 8) != raw) {
                db_log("romfs: GZIP block size mismatch\n");
                return -1;
            }
            tabl = malloc(raw);
            if (!tabl) return -1;
            mio0_decompress((void *)(data + 8), tabl);
            tabl_size = raw;
        }
        if (tag == TAG('T', 'A', 'B', 'L')) {
            const uint8_t *t = tabl ? tabl : data;
            for (uint32_t i = 0; i + 8 <= tabl_size; i += 8) {
                uint32_t etag = be32(t + i), esize = be32(t + i + 4);
                c->index_entries++;
                if (etag != 0) {
                    const uint8_t *file = romfs_rom_ptr(fs_off);
                    c->uv_files++;
                    count_tag(c, etag);
                    if (file && be32(file) == TAG('F', 'O', 'R', 'M') && be32(file + 8) == etag)
                        c->form_ok++;
                }
                fs_off += esize;
            }
        }
        free(tabl);
        off += 8 + size;
    }
    c->walk_end = fs_off;
    return (c->uv_files > 0 && c->form_ok == c->uv_files) ? 0 : -1;
}
