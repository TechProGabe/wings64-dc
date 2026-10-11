/* hosttest.c — host (ILP32 ARM under qemu) tests for M1.
 *   hosttest            unit tests that need no ROM data
 *   hosttest <discdir>  + the M0 boot check on the host: load <discdir>/wings64.fs, CRC, MIO0 index walk
 * The decomp is linked in full (bootproc referenced) so the host link proves the game resolves.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rom/romfs.h"
#include <sh4zam/shz_sh4zam.h>

void pw64_swap(void *p, unsigned int count, unsigned int stride, const char *layout);
extern void bootproc(void *arg);
void (*volatile wings64_decomp_entry)(void *) = bootproc;

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_swap(void) {
    /* A Vtx-like record: 3 s16 ob, s16 flag, 2 s16 tc, 4 u8 cn  ->  "6h4b" (16 bytes). */
    uint8_t v[16] = { 0x00,0x01, 0x00,0x02, 0xFF,0xFE, 0x12,0x34, 0x01,0x00, 0x02,0x00, 1,2,3,4 };
    int16_t *h = (int16_t *)v;
    pw64_swap(v, 1, 16, "6h4b");
    CHECK(h[0] == 1 && h[1] == 2 && h[2] == -2 && h[3] == 0x1234 && h[4] == 0x100 && h[5] == 0x200, "6h swap");
    CHECK(v[12] == 1 && v[15] == 4, "4b kept");
    uint8_t w[8] = { 0x11,0x22,0x33,0x44, 0xAA,0xBB,0xCC,0xDD };
    pw64_swap(w, 2, 4, "w");
    CHECK(*(uint32_t *)w == 0x11223344u && *(uint32_t *)(w + 4) == 0xAABBCCDDu, "w swap x2: BE bytes read back as the BE value");
}

static void test_shz(void) {
    shz_vec4_t p;
    shz_xmtrx_init_translation(1.0f, 2.0f, 3.0f);
    p = shz_xmtrx_transform_vec4(shz_vec4_init(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK(p.x == 2.0f && p.y == 3.0f && p.z == 4.0f && p.w == 1.0f, "sh4zam SW xmtrx translation");
}

static void test_romfs(const char *dir) {
    romfs_meta_t meta;
    romfs_counts_t c;
    CHECK(romfs_meta_read(dir, &meta) == 0, "fsmeta.txt readable");
    CHECK(romfs_load(dir, NULL) == 0, "wings64.fs loads");
    if (!romfs_size) return;
    CHECK(romfs_crc32() == meta.crc32, "crc32 %08lX vs manifest %08lX", (unsigned long)romfs_crc32(), (unsigned long)meta.crc32);
    CHECK(romfs_count(&c) == 0, "index walk");
    CHECK(c.uv_files == meta.uv_files && c.form_ok == c.uv_files, "uv_files %lu form_ok %lu", (unsigned long)c.uv_files, (unsigned long)c.form_ok);
    CHECK(c.walk_end == ROMFS_FILESYS_END, "walk end %06lX", (unsigned long)c.walk_end);
    printf("romfs: %lu bytes, %lu UV files, walk end %06lX\n", (unsigned long)romfs_size, (unsigned long)c.uv_files, (unsigned long)c.walk_end);
}

int main(int argc, char **argv) {
    printf("hosttest: ILP32 %s, decomp bootproc @ %p\n", sizeof(void *) == 4 ? "yes" : "NO", (void *)wings64_decomp_entry);
    CHECK(sizeof(void *) == 4, "host build must be ILP32");
    test_swap();
    test_shz();
    if (argc > 1) test_romfs(argv[1]);
    printf(failures ? "hosttest: %d FAILED\n" : "hosttest: OK\n", failures);
    return failures != 0;
}
