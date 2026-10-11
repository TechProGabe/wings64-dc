/* test_texconv.c - standalone host test for src/dc/gfx/gfx_texconv.c. No KOS. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "gfx/gfx_texconv.h"

static int failures;

static void expect_u(const char *what, unsigned got, unsigned want)
{
    if (got != want) {
        failures++;
        fprintf(stderr, "FAIL %s: got 0x%x want 0x%x\n", what, got, want);
    }
}

static void expect_mem(const char *what, const void *got, const void *want, size_t n)
{
    if (memcmp(got, want, n) != 0) {
        failures++;
        fprintf(stderr, "FAIL %s: buffer mismatch\n", what);
    }
}

/* Reference twiddle: walks the Morton order in reverse (k -> lx/ly by bit de-interleave)
   and writes dst[block*s*s + k] = src at that texel. Independent of the implementation's loop. */
static void ref_twiddle(const uint16_t *src, uint16_t *dst, unsigned w, unsigned h)
{
    unsigned s = w < h ? w : h;
    unsigned blocks = w > h ? w / s : h / s;
    for (unsigned blk = 0; blk < blocks; blk++) {
        for (unsigned k = 0; k < s * s; k++) {
            unsigned lx = 0, ly = 0;
            for (unsigned b = 0; b < 4; b++) {
                ly |= ((k >> (2 * b)) & 1u) << b;
                lx |= ((k >> (2 * b + 1)) & 1u) << b;
            }
            unsigned x = w > h ? blk * s + lx : lx;
            unsigned y = w > h ? ly : blk * s + ly;
            dst[blk * s * s + k] = src[y * w + x];
        }
    }
}

static void test_target(void)
{
    expect_u("target RGBA16", texconv_target(N64FMT_RGBA, N64SIZ_16b), PVRFMT_ARGB1555);
    expect_u("target RGBA32", texconv_target(N64FMT_RGBA, N64SIZ_32b), PVRFMT_ARGB4444);
    expect_u("target RGBA4b unsupported", texconv_target(N64FMT_RGBA, N64SIZ_4b), PVRFMT_UNSUPPORTED);
    expect_u("target IA4", texconv_target(N64FMT_IA, N64SIZ_4b), PVRFMT_ARGB1555);
    expect_u("target IA8", texconv_target(N64FMT_IA, N64SIZ_8b), PVRFMT_ARGB4444);
    expect_u("target IA16", texconv_target(N64FMT_IA, N64SIZ_16b), PVRFMT_ARGB4444);
    expect_u("target I4", texconv_target(N64FMT_I, N64SIZ_4b), PVRFMT_ARGB4444);
    expect_u("target I8", texconv_target(N64FMT_I, N64SIZ_8b), PVRFMT_ARGB4444);
    expect_u("target I16 unsupported", texconv_target(N64FMT_I, N64SIZ_16b), PVRFMT_UNSUPPORTED);
    expect_u("target CI unsupported", texconv_target(N64FMT_CI, N64SIZ_8b), PVRFMT_UNSUPPORTED);
    expect_u("target YUV unsupported", texconv_target(N64FMT_YUV, N64SIZ_16b), PVRFMT_UNSUPPORTED);
}

static void test_line_bytes(void)
{
    expect_u("line 3 x16b", texconv_line_bytes(3, N64SIZ_16b), 8);   /* 6 -> 8 */
    expect_u("line 8 x16b", texconv_line_bytes(8, N64SIZ_16b), 16);  /* 16 */
    expect_u("line 5 x4b", texconv_line_bytes(5, N64SIZ_4b), 8);     /* 3 -> 8 */
    expect_u("line 32 x4b", texconv_line_bytes(32, N64SIZ_4b), 16);  /* 16 */
    expect_u("line 4 x32b", texconv_line_bytes(4, N64SIZ_32b), 16);  /* 16 */
}

static void test_pow2(void)
{
    expect_u("pow2(1)", texconv_pow2(1), 8);
    expect_u("pow2(5)", texconv_pow2(5), 8);
    expect_u("pow2(8)", texconv_pow2(8), 8);
    expect_u("pow2(9)", texconv_pow2(9), 16);
    expect_u("pow2(47)", texconv_pow2(47), 64);
    expect_u("pow2(1024)", texconv_pow2(1024), 1024);
    expect_u("pow2(1025)", texconv_pow2(1025), 1024);
}

static void test_deswizzle(void)
{
    /* Row 0 (even) is untouched. Row 1 (odd): each 8-byte group swaps its 4-byte halves.
       Before: 10 11 12 13 14 15 16 17   After: 14 15 16 17 10 11 12 13 */
    uint8_t buf[16];
    uint8_t want[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                         0x14, 0x15, 0x16, 0x17, 0x10, 0x11, 0x12, 0x13 };
    for (unsigned i = 0; i < 16; i++)
        buf[i] = (uint8_t)(i < 8 ? i : 0x10 + (i - 8));
    texconv_deswizzle(buf, 8, 2);
    expect_mem("deswizzle 2 rows", buf, want, 16);
}

static void test_convert_formats(void)
{
    uint8_t src[8];
    uint16_t out[4];
    texconv_pvrfmt_t r;

    /* RGBA16, 3 texels (line 8), dst 4x1 so x=3 is clamp-padded from x=2.
       0xF801 = R31 G0 B0 A1   -> A1 R31 G0 B0          = 0xFC00
       0x07C0 = R0 G31 B0 A0   -> A0 R0 G31 B0          = 0x03E0
       0x550B = R10 G20 B5 A1  -> A1 R10 G20 B5         = 0xAA85 */
    memset(src, 0, sizeof src);
    src[0] = 0xF8; src[1] = 0x01;
    src[2] = 0x07; src[3] = 0xC0;
    src[4] = 0x55; src[5] = 0x0B;
    memset(out, 0, sizeof out);
    r = texconv_convert(src, 8, 3, 1, N64FMT_RGBA, N64SIZ_16b, 1, out, 4, 1);
    expect_u("rgba16 fmt", r, PVRFMT_ARGB1555);
    expect_u("rgba16 t0", out[0], 0xFC00);
    expect_u("rgba16 t1", out[1], 0x03E0);
    expect_u("rgba16 t2", out[2], 0xAA85);
    expect_u("rgba16 pad", out[3], 0xAA85);

    /* IA4: byte 0xE1 -> texel0 nibble 0xE (I3=7 A0): I5=31 -> 0x7FFF;
                       texel1 nibble 0x1 (I3=0 A1): I5=0 -> 0x8000 */
    memset(src, 0, sizeof src);
    src[0] = 0xE1;
    r = texconv_convert(src, 8, 2, 1, N64FMT_IA, N64SIZ_4b, 1, out, 2, 1);
    expect_u("ia4 fmt", r, PVRFMT_ARGB1555);
    expect_u("ia4 t0", out[0], 0x7FFF);
    expect_u("ia4 t1", out[1], 0x8000);

    /* I4: byte 0xA5 -> texel0 I=10, texel1 I=5.
       alpha from intensity: 0xAAAA, 0x5555. Opaque alpha: 0xFAAA, 0xF555. */
    src[0] = 0xA5;
    r = texconv_convert(src, 8, 2, 1, N64FMT_I, N64SIZ_4b, 1, out, 2, 1);
    expect_u("i4 fmt", r, PVRFMT_ARGB4444);
    expect_u("i4 a t0", out[0], 0xAAAA);
    expect_u("i4 a t1", out[1], 0x5555);
    r = texconv_convert(src, 8, 2, 1, N64FMT_I, N64SIZ_4b, 0, out, 2, 1);
    expect_u("i4 opaque t0", out[0], 0xFAAA);
    expect_u("i4 opaque t1", out[1], 0xF555);

    /* I8: byte 0x80 -> I4=8 -> 0x8888 (alpha from I) or 0xF888 (opaque); 0xF0 -> 0xFFFF both. */
    src[0] = 0x80; src[1] = 0xF0;
    r = texconv_convert(src, 8, 2, 1, N64FMT_I, N64SIZ_8b, 1, out, 2, 1);
    expect_u("i8 fmt", r, PVRFMT_ARGB4444);
    expect_u("i8 a t0", out[0], 0x8888);
    expect_u("i8 a t1", out[1], 0xFFFF);
    r = texconv_convert(src, 8, 2, 1, N64FMT_I, N64SIZ_8b, 0, out, 2, 1);
    expect_u("i8 opaque t0", out[0], 0xF888);

    /* IA8: byte 0x3C -> I=3 A=12 -> 0xC333. byte 0xF1 -> I=15 A=1 -> 0x1FFF. */
    src[0] = 0x3C; src[1] = 0xF1;
    r = texconv_convert(src, 8, 2, 1, N64FMT_IA, N64SIZ_8b, 1, out, 2, 1);
    expect_u("ia8 fmt", r, PVRFMT_ARGB4444);
    expect_u("ia8 t0", out[0], 0xC333);
    expect_u("ia8 t1", out[1], 0x1FFF);

    /* IA16: I=0xF0 A=0x40 -> 0x4FFF. I=0x20 A=0xF0 -> 0xF222. */
    src[0] = 0xF0; src[1] = 0x40; src[2] = 0x20; src[3] = 0xF0;
    r = texconv_convert(src, 8, 2, 1, N64FMT_IA, N64SIZ_16b, 1, out, 2, 1);
    expect_u("ia16 fmt", r, PVRFMT_ARGB4444);
    expect_u("ia16 t0", out[0], 0x4FFF);
    expect_u("ia16 t1", out[1], 0xF222);

    /* RGBA32: FF 80 10 20 -> R15 G8 B1 A2 -> 0x2F81. 00 00 00 FF -> 0xF000. */
    memset(src, 0, sizeof src);
    src[0] = 0xFF; src[1] = 0x80; src[2] = 0x10; src[3] = 0x20;
    src[4] = 0x00; src[5] = 0x00; src[6] = 0x00; src[7] = 0xFF;
    r = texconv_convert(src, 8, 2, 1, N64FMT_RGBA, N64SIZ_32b, 1, out, 2, 1);
    expect_u("rgba32 fmt", r, PVRFMT_ARGB4444);
    expect_u("rgba32 t0", out[0], 0x2F81);
    expect_u("rgba32 t1", out[1], 0xF000);

    /* Unsupported format and undersized dst are refused. */
    r = texconv_convert(src, 8, 2, 1, N64FMT_CI, N64SIZ_8b, 1, out, 2, 1);
    expect_u("ci refused", r, PVRFMT_UNSUPPORTED);
    r = texconv_convert(src, 8, 2, 2, N64FMT_I, N64SIZ_8b, 1, out, 2, 1);
    expect_u("dst too small refused", r, PVRFMT_UNSUPPORTED);
}

static void test_clamp_pad(void)
{
    /* 3x3 RGBA16 source, line 8. texel (x,y) = 1555 value v = ((y*3+x+1)<<1)|1, big-endian.
       Expected ARGB1555: A=1, B=(v>>1)&31 = y*3+x+1, others 0. */
    uint8_t src[3 * 8];
    uint16_t dst[8 * 8];
    memset(src, 0, sizeof src);
    for (unsigned y = 0; y < 3; y++) {
        for (unsigned x = 0; x < 3; x++) {
            unsigned v = ((y * 3 + x + 1) << 1) | 1;
            src[y * 8 + 2 * x] = (uint8_t)(v >> 8);
            src[y * 8 + 2 * x + 1] = (uint8_t)(v & 0xFF);
        }
    }
    texconv_pvrfmt_t r = texconv_convert(src, 8, 3, 3, N64FMT_RGBA, N64SIZ_16b, 1, dst, 8, 8);
    expect_u("pad fmt", r, PVRFMT_ARGB1555);
    expect_u("pad (2,2)", dst[2 * 8 + 2], 0x8009);      /* texel 8 -> B=9 */
    expect_u("pad (7,7)==(2,2)", dst[7 * 8 + 7], 0x8009);
    expect_u("pad (7,0)==(2,0)", dst[0 * 8 + 7], 0x8003); /* texel 2 -> B=3 */
    expect_u("pad (0,7)==(0,2)", dst[7 * 8 + 0], 0x8007); /* texel 6 -> B=7 */
    expect_u("pad (5,6)==(2,2)", dst[6 * 8 + 5], 0x8009);
}

static void test_twiddle(void)
{
    uint16_t src[32], got[32], want[32];

    /* 4x4: Morton with y in bit 0. Index of (x,y): (1,0)=2, (0,1)=1, (2,0)=8, (0,2)=4. */
    for (unsigned i = 0; i < 16; i++)
        src[i] = (uint16_t)(i + 1);
    memset(got, 0, sizeof got);
    memset(want, 0, sizeof want);
    texconv_twiddle16(src, got, 4, 4);
    ref_twiddle(src, want, 4, 4);
    expect_mem("twiddle 4x4 vs ref", got, want, 16 * sizeof(uint16_t));
    expect_u("twiddle 4x4 (1,0)->2", got[2], src[1]);
    expect_u("twiddle 4x4 (0,1)->1", got[1], src[4]);
    expect_u("twiddle 4x4 (2,0)->8", got[8], src[2]);
    expect_u("twiddle 4x4 (0,2)->4", got[4], src[8]);

    /* 8x4: two 4x4 square blocks laid out along x. */
    for (unsigned i = 0; i < 32; i++)
        src[i] = (uint16_t)(i + 1);
    memset(got, 0, sizeof got);
    memset(want, 0, sizeof want);
    texconv_twiddle16(src, got, 8, 4);
    ref_twiddle(src, want, 8, 4);
    expect_mem("twiddle 8x4 vs ref", got, want, 32 * sizeof(uint16_t));
    expect_u("twiddle 8x4 block1 first", got[16], src[4]);

    /* 4x8: two 4x4 blocks stacked along y. */
    memset(got, 0, sizeof got);
    memset(want, 0, sizeof want);
    texconv_twiddle16(src, got, 4, 8);
    ref_twiddle(src, want, 4, 8);
    expect_mem("twiddle 4x8 vs ref", got, want, 32 * sizeof(uint16_t));
}

int main(void)
{
    test_target();
    test_line_bytes();
    test_pow2();
    test_deswizzle();
    test_convert_formats();
    test_clamp_pad();
    test_twiddle();

    if (failures) {
        fprintf(stderr, "test_texconv: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_texconv: OK\n");
    return 0;
}
