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
            for (unsigned b = 0; b < 10; b++) {
                ly |= ((k >> (2 * b)) & 1u) << b;
                lx |= ((k >> (2 * b + 1)) & 1u) << b;
            }
            unsigned x = w > h ? blk * s + lx : lx;
            unsigned y = w > h ? ly : blk * s + ly;
            dst[blk * s * s + k] = src[y * w + x];
        }
    }
}
/* Deterministic pseudo-random source data (LCG, 15 bits per call). */
static uint32_t rng_state = 12345;

static unsigned rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFF;
}

static void fill_rand_bytes(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)rng_next();
}

static void fill_rand_u16(uint16_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint16_t)((rng_next() << 1) ^ rng_next());
}

/* Reference convert: the original per-texel logic (copy of the pre-optimisation code). */
static uint16_t ref_pix1555(unsigned r, unsigned g, unsigned b, unsigned a)
{
    return (uint16_t)((a << 15) | (r << 10) | (g << 5) | b);
}

static uint16_t ref_pix4444(unsigned r, unsigned g, unsigned b, unsigned a)
{
    return (uint16_t)((a << 12) | (r << 8) | (g << 4) | b);
}

static unsigned ref_nibble(const uint8_t *row, unsigned x)
{
    uint8_t byte = row[x >> 1];
    return (x & 1) ? (byte & 0xF) : (byte >> 4);
}

static uint16_t ref_texel(const uint8_t *row, unsigned x, n64_fmt_t fmt, n64_siz_t siz, int ia)
{
    if (fmt == N64FMT_RGBA) {
        if (siz == N64SIZ_16b) {
            unsigned v = ((unsigned)row[2 * x] << 8) | row[2 * x + 1];
            return ref_pix1555((v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31, v & 1);
        }
        return ref_pix4444(row[4 * x] >> 4, row[4 * x + 1] >> 4, row[4 * x + 2] >> 4, row[4 * x + 3] >> 4);
    }
    if (fmt == N64FMT_IA) {
        if (siz == N64SIZ_4b) {
            unsigned n = ref_nibble(row, x);
            unsigned i3 = n >> 1;
            unsigned i5 = (i3 << 2) | (i3 >> 1);
            return ref_pix1555(i5, i5, i5, n & 1);
        }
        if (siz == N64SIZ_8b) {
            unsigned i4 = row[x] >> 4;
            return ref_pix4444(i4, i4, i4, row[x] & 0xF);
        }
        unsigned i4 = row[2 * x] >> 4;
        return ref_pix4444(i4, i4, i4, row[2 * x + 1] >> 4);
    }
    if (siz == N64SIZ_4b) {
        unsigned n = ref_nibble(row, x);
        return ref_pix4444(n, n, n, ia ? n : 0xF);
    }
    unsigned i4 = row[x] >> 4;
    return ref_pix4444(i4, i4, i4, ia ? i4 : 0xF);
}

static texconv_pvrfmt_t ref_convert(const uint8_t *src, unsigned line_bytes, unsigned w, unsigned h,
                                    n64_fmt_t fmt, n64_siz_t siz, int ia, uint16_t *dst,
                                    unsigned dst_w, unsigned dst_h)
{
    texconv_pvrfmt_t tgt = texconv_target(fmt, siz);
    if (tgt == PVRFMT_UNSUPPORTED)
        return tgt;
    for (unsigned dy = 0; dy < dst_h; dy++) {
        unsigned sy = dy < h ? dy : h - 1;
        const uint8_t *row = src + sy * line_bytes;
        for (unsigned dx = 0; dx < dst_w; dx++) {
            unsigned sx = dx < w ? dx : w - 1;
            dst[dy * dst_w + dx] = ref_texel(row, sx, fmt, siz, ia);
        }
    }
    return tgt;
}

static void test_convert_random(void)
{
    static const struct { n64_fmt_t fmt; n64_siz_t siz; } fmts[] = {
        { N64FMT_RGBA, N64SIZ_16b }, { N64FMT_RGBA, N64SIZ_32b },
        { N64FMT_IA, N64SIZ_4b }, { N64FMT_IA, N64SIZ_8b }, { N64FMT_IA, N64SIZ_16b },
        { N64FMT_I, N64SIZ_4b }, { N64FMT_I, N64SIZ_8b },
    };
    /* w, h, dst_w, dst_h */
    static const unsigned shapes[][4] = {
        { 1, 1, 1, 1 }, { 1, 1, 8, 8 }, { 13, 7, 16, 8 }, { 3, 3, 8, 8 }, { 5, 9, 16, 16 },
        { 32, 4, 32, 8 }, { 7, 5, 7, 5 }, { 1, 5, 8, 8 }, { 6, 1, 8, 2 }, { 17, 11, 32, 16 },
        { 64, 3, 64, 8 }, { 9, 9, 16, 16 }, { 2, 6, 8, 16 },
    };
    static uint8_t src[4096];
    static uint16_t got[1024], want[1024];

    for (unsigned f = 0; f < sizeof fmts / sizeof fmts[0]; f++) {
        for (unsigned s = 0; s < sizeof shapes / sizeof shapes[0]; s++) {
            for (int ia = 0; ia < 2; ia++) {
                unsigned w = shapes[s][0], h = shapes[s][1];
                unsigned dw = shapes[s][2], dh = shapes[s][3];
                unsigned lb = texconv_line_bytes(w, fmts[f].siz);
                fill_rand_bytes(src, lb * h);
                memset(got, 0xAA, sizeof got);
                memset(want, 0xAA, sizeof want);
                texconv_pvrfmt_t r = texconv_convert(src, lb, w, h, fmts[f].fmt, fmts[f].siz, ia,
                                                     got, dw, dh);
                texconv_pvrfmt_t rw = ref_convert(src, lb, w, h, fmts[f].fmt, fmts[f].siz, ia,
                                                  want, dw, dh);
                char what[96];
                snprintf(what, sizeof what, "convert fmt%u siz%u %ux%u->%ux%u ia%d",
                         (unsigned)fmts[f].fmt, (unsigned)fmts[f].siz, w, h, dw, dh, ia);
                expect_u(what, r, rw);
                expect_mem(what, got, want, dw * dh * sizeof(uint16_t));
            }
        }
    }

    /* Unsupported combos are refused. */
    static const struct { n64_fmt_t fmt; n64_siz_t siz; } bad[] = {
        { N64FMT_RGBA, N64SIZ_4b }, { N64FMT_RGBA, N64SIZ_8b }, { N64FMT_IA, N64SIZ_32b },
        { N64FMT_I, N64SIZ_16b }, { N64FMT_I, N64SIZ_32b }, { N64FMT_CI, N64SIZ_4b },
        { N64FMT_YUV, N64SIZ_16b },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        texconv_pvrfmt_t r = texconv_convert(src, 8, 2, 2, bad[i].fmt, bad[i].siz, 1, got, 2, 2);
        expect_u("convert unsupported refused", r, PVRFMT_UNSUPPORTED);
    }
}

static void ref_resample(const uint16_t *src, unsigned w, unsigned h, uint16_t *dst, unsigned pw, unsigned ph)
{
    for (unsigned y = 0; y < ph; y++)
        for (unsigned x = 0; x < pw; x++)
            dst[y * pw + x] = src[(y * h / ph) * w + (x * w / pw)];
}

static void test_resample(void)
{
    /* w, h, pw, ph */
    static const unsigned cases[][4] = {
        { 13, 7, 16, 8 }, { 64, 64, 64, 64 }, { 100, 3, 128, 8 }, { 1, 1, 8, 8 },
        { 320, 20, 512, 32 }, { 9, 4, 4, 2 }, { 7, 2, 3, 1 }, { 9, 4, 1, 1 },
    };
    static uint16_t src[8192], got[16384], want[16384];
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        unsigned w = cases[c][0], h = cases[c][1], pw = cases[c][2], ph = cases[c][3];
        fill_rand_u16(src, w * h);
        memset(got, 0, sizeof got);
        memset(want, 0, sizeof want);
        texconv_resample_pow2(src, w, h, got, pw, ph);
        ref_resample(src, w, h, want, pw, ph);
        char what[96];
        snprintf(what, sizeof what, "resample %ux%u->%ux%u", w, h, pw, ph);
        expect_mem(what, got, want, pw * ph * sizeof(uint16_t));
    }
}

static void test_twiddle_sizes(void)
{
    /* w, h */
    static const unsigned dims[][2] = {
        { 1024, 32 }, { 32, 1024 }, { 64, 64 }, { 8, 8 }, { 16, 64 }, { 64, 16 },
        { 1, 1 }, { 1, 8 }, { 8, 1 }, { 2, 2 }, { 128, 8 }, { 8, 128 },
    };
    static uint16_t src[1024 * 32], got[1024 * 32], want[1024 * 32];
    for (unsigned d = 0; d < sizeof dims / sizeof dims[0]; d++) {
        unsigned w = dims[d][0], h = dims[d][1];
        fill_rand_u16(src, w * h);
        memset(got, 0, sizeof got);
        memset(want, 0, sizeof want);
        texconv_twiddle16(src, got, w, h);
        ref_twiddle(src, want, w, h);
        char what[64];
        snprintf(what, sizeof what, "twiddle %ux%u vs ref", w, h);
        expect_mem(what, got, want, w * h * sizeof(uint16_t));
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
    test_convert_random();
    test_twiddle_sizes();
    test_resample();

    if (failures) {
        fprintf(stderr, "test_texconv: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_texconv: OK\n");
    return 0;
}
