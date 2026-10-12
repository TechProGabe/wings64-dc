/* gfx_texconv.c - N64 texture -> Dreamcast PVR texel conversion. Pure C, no KOS calls. */
/* Kill switch: 0 compiles the original per-texel code paths. */
#ifndef WINGS64_FAST_TEXCONV
#define WINGS64_FAST_TEXCONV 1
#endif

#include <string.h>
#include "gfx_texconv.h"

static unsigned bits_per_texel(n64_siz_t siz)
{
    switch (siz) {
    case N64SIZ_4b:  return 4;
    case N64SIZ_8b:  return 8;
    case N64SIZ_16b: return 16;
    case N64SIZ_32b: return 32;
    }
    return 0;
}

texconv_pvrfmt_t texconv_target(n64_fmt_t fmt, n64_siz_t siz)
{
    switch (fmt) {
    case N64FMT_RGBA:
        if (siz == N64SIZ_16b) return PVRFMT_ARGB1555;
        if (siz == N64SIZ_32b) return PVRFMT_ARGB4444;
        break;
    case N64FMT_IA:
        if (siz == N64SIZ_4b) return PVRFMT_ARGB1555;
        if (siz == N64SIZ_8b || siz == N64SIZ_16b) return PVRFMT_ARGB4444;
        break;
    case N64FMT_I:
        if (siz == N64SIZ_4b || siz == N64SIZ_8b) return PVRFMT_ARGB4444;
        break;
    default:
        break;
    }
    return PVRFMT_UNSUPPORTED;
}

unsigned texconv_line_bytes(unsigned width, n64_siz_t siz)
{
    unsigned bytes = (width * bits_per_texel(siz) + 7) / 8;
    return (bytes + 7) & ~7u;
}

void texconv_deswizzle(uint8_t *data, unsigned line_bytes, unsigned rows)
{
    for (unsigned y = 1; y < rows; y += 2) {
        uint8_t *row = data + y * line_bytes;
        for (unsigned g = 0; g + 8 <= line_bytes; g += 8) {
            for (unsigned i = 0; i < 4; i++) {
                uint8_t t = row[g + i];
                row[g + i] = row[g + 4 + i];
                row[g + 4 + i] = t;
            }
        }
    }
}

/* LOADBLOCK writes 64-bit word i to TMEM with its 32-bit halves swapped when the RDP's line counter
   (i * dxt, 11-bit fraction) is on an odd line; sampling swaps back on odd tile rows. So RAM word i shows
   up swapped exactly when those two parities differ. dxt 0 (pre-swizzled data) is texconv_deswizzle;
   a dxt matching the tile line needs nothing; any other dxt (a load sized for a different width) used to
   come out striped. */
void texconv_deswizzle_dxt(uint8_t *data, unsigned line_bytes, unsigned rows, unsigned dxt)
{
    unsigned words = line_bytes / 8;
    if (!words) return;
    for (unsigned y = 0; y < rows; y++) {
        uint8_t *row = data + y * line_bytes;
        for (unsigned w = 0; w < words; w++) {
            unsigned i = y * words + w;
            if ((((i * dxt) >> 11) & 1) == (y & 1)) continue;
            for (unsigned b = 0; b < 4; b++) {
                uint8_t t = row[w * 8 + b];
                row[w * 8 + b] = row[w * 8 + 4 + b];
                row[w * 8 + 4 + b] = t;
            }
        }
    }
}

unsigned texconv_pow2(unsigned n)
{
    unsigned p = 8;
    while (p < n && p < 1024)
        p <<= 1;
    return p;
}

static uint16_t pix1555(unsigned r, unsigned g, unsigned b, unsigned a)
{
    return (uint16_t)((a << 15) | (r << 10) | (g << 5) | b);
}

static uint16_t pix4444(unsigned r, unsigned g, unsigned b, unsigned a)
{
    return (uint16_t)((a << 12) | (r << 8) | (g << 4) | b);
}

/* 4-bit texel x of a row: even texels in the high nibble. */
static unsigned nibble(const uint8_t *row, unsigned x)
{
    uint8_t byte = row[x >> 1];
    return (x & 1) ? (byte & 0xF) : (byte >> 4);
}

#if !WINGS64_FAST_TEXCONV
/* Decode one texel from row start and encode it in the PVR format chosen by texconv_target(). */
static uint16_t texel_to_pvr(const uint8_t *row, unsigned x, n64_fmt_t fmt, n64_siz_t siz, int ia)
{
    if (fmt == N64FMT_RGBA) {
        if (siz == N64SIZ_16b) {
            unsigned v = ((unsigned)row[2 * x] << 8) | row[2 * x + 1];
            return pix1555((v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31, v & 1);
        }
        /* RGBA32: R,G,B,A bytes */
        return pix4444(row[4 * x] >> 4, row[4 * x + 1] >> 4, row[4 * x + 2] >> 4, row[4 * x + 3] >> 4);
    }
    if (fmt == N64FMT_IA) {
        if (siz == N64SIZ_4b) {
            unsigned n = nibble(row, x);
            unsigned i3 = n >> 1;
            unsigned i5 = (i3 << 2) | (i3 >> 1);
            return pix1555(i5, i5, i5, n & 1);
        }
        if (siz == N64SIZ_8b) {
            unsigned i4 = row[x] >> 4;
            return pix4444(i4, i4, i4, row[x] & 0xF);
        }
        /* IA16: I byte then A byte */
        unsigned i4 = row[2 * x] >> 4;
        return pix4444(i4, i4, i4, row[2 * x + 1] >> 4);
    }
    /* N64FMT_I */
    if (siz == N64SIZ_4b) {
        unsigned n = nibble(row, x);
        return pix4444(n, n, n, ia ? n : 0xF);
    }
    unsigned i4 = row[x] >> 4;
    return pix4444(i4, i4, i4, ia ? i4 : 0xF);
}

texconv_pvrfmt_t texconv_convert(const uint8_t *src, unsigned line_bytes, unsigned w, unsigned h,
                                 n64_fmt_t fmt, n64_siz_t siz, int i_alpha_from_intensity,
                                 uint16_t *dst, unsigned dst_w, unsigned dst_h)
{
    texconv_pvrfmt_t tgt = texconv_target(fmt, siz);
    if (tgt == PVRFMT_UNSUPPORTED || !src || !dst)
        return PVRFMT_UNSUPPORTED;
    if (w == 0 || h == 0 || dst_w < w || dst_h < h)
        return PVRFMT_UNSUPPORTED;
    if ((w * bits_per_texel(siz) + 7) / 8 > line_bytes)
        return PVRFMT_UNSUPPORTED;

    for (unsigned dy = 0; dy < dst_h; dy++) {
        unsigned sy = dy < h ? dy : h - 1;
        const uint8_t *row = src + sy * line_bytes;
        for (unsigned dx = 0; dx < dst_w; dx++) {
            unsigned sx = dx < w ? dx : w - 1;
            dst[dy * dst_w + dx] = texel_to_pvr(row, sx, fmt, siz, i_alpha_from_intensity);
        }
    }
    return tgt;
}
#else
/* Row converters: each writes exactly w texels of one source row. Chosen once per call. */
typedef void (*row_fn)(const uint8_t *row, uint16_t *d, unsigned w, int ia);

static void row_rgba16(const uint8_t *row, uint16_t *d, unsigned w, int ia)
{
    (void)ia;
    for (unsigned x = 0; x < w; x++) {
        unsigned v = ((unsigned)row[2 * x] << 8) | row[2 * x + 1];
        d[x] = pix1555((v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31, v & 1);
    }
}

static void row_rgba32(const uint8_t *row, uint16_t *d, unsigned w, int ia)
{
    (void)ia;
    for (unsigned x = 0; x < w; x++) {
        const uint8_t *p = row + 4 * x;
        d[x] = pix4444(p[0] >> 4, p[1] >> 4, p[2] >> 4, p[3] >> 4);
    }
}

static void row_ia4(const uint8_t *row, uint16_t *d, unsigned w, int ia)
{
    (void)ia;
    for (unsigned x = 0; x < w; x++) {
        unsigned n = nibble(row, x);
        unsigned i3 = n >> 1;
        unsigned i5 = (i3 << 2) | (i3 >> 1);
        d[x] = pix1555(i5, i5, i5, n & 1);
    }
}

static void row_ia8(const uint8_t *row, uint16_t *d, unsigned w, int ia)
{
    (void)ia;
    for (unsigned x = 0; x < w; x++) {
        unsigned i4 = row[x] >> 4;
        d[x] = pix4444(i4, i4, i4, row[x] & 0xF);
    }
}

static void row_ia16(const uint8_t *row, uint16_t *d, unsigned w, int ia)
{
    (void)ia;
    for (unsigned x = 0; x < w; x++) {
        unsigned i4 = row[2 * x] >> 4;
        d[x] = pix4444(i4, i4, i4, row[2 * x + 1] >> 4);
    }
}

static void row_i4(const uint8_t *row, uint16_t *d, unsigned w, int ia)
{
    for (unsigned x = 0; x < w; x++) {
        unsigned n = nibble(row, x);
        d[x] = pix4444(n, n, n, ia ? n : 0xF);
    }
}

static void row_i8(const uint8_t *row, uint16_t *d, unsigned w, int ia)
{
    for (unsigned x = 0; x < w; x++) {
        unsigned i4 = row[x] >> 4;
        d[x] = pix4444(i4, i4, i4, ia ? i4 : 0xF);
    }
}

texconv_pvrfmt_t texconv_convert(const uint8_t *src, unsigned line_bytes, unsigned w, unsigned h,
                                 n64_fmt_t fmt, n64_siz_t siz, int i_alpha_from_intensity,
                                 uint16_t *dst, unsigned dst_w, unsigned dst_h)
{
    texconv_pvrfmt_t tgt = texconv_target(fmt, siz);
    if (tgt == PVRFMT_UNSUPPORTED || !src || !dst)
        return PVRFMT_UNSUPPORTED;
    if (w == 0 || h == 0 || dst_w < w || dst_h < h)
        return PVRFMT_UNSUPPORTED;
    if ((w * bits_per_texel(siz) + 7) / 8 > line_bytes)
        return PVRFMT_UNSUPPORTED;

    /* Dispatch on (fmt, siz) once; the row loops below run the per-format converter. */
    row_fn fn;
    if (fmt == N64FMT_RGBA)
        fn = siz == N64SIZ_16b ? row_rgba16 : row_rgba32;
    else if (fmt == N64FMT_IA)
        fn = siz == N64SIZ_4b ? row_ia4 : siz == N64SIZ_8b ? row_ia8 : row_ia16;
    else
        fn = siz == N64SIZ_4b ? row_i4 : row_i8;

    /* Real rows: convert w texels, then clamp-pad columns w..dst_w-1 with the last texel. */
    for (unsigned dy = 0; dy < h; dy++) {
        uint16_t *d = dst + dy * dst_w;
        fn(src + dy * line_bytes, d, w, i_alpha_from_intensity);
        for (unsigned dx = w; dx < dst_w; dx++)
            d[dx] = d[w - 1];
    }
    /* Padding rows repeat the last real destination row. */
    for (unsigned dy = h; dy < dst_h; dy++)
        memcpy(dst + dy * dst_w, dst + (h - 1) * dst_w, dst_w * sizeof(uint16_t));
    return tgt;
}
#endif

/* Morton index within an s x s block (s pow2): bit 0 = y0, bit 1 = x0, bit 2 = y1, ... */
static unsigned morton(unsigned x, unsigned y, unsigned s)
{
    unsigned idx = 0;
    for (unsigned b = 0; (1u << b) < s; b++) {
        idx |= ((y >> b) & 1u) << (2 * b);
        idx |= ((x >> b) & 1u) << (2 * b + 1);
    }
    return idx;
}

#if WINGS64_FAST_TEXCONV
/* Morton spread tables: tx[x] = x bits at odd positions, ty[y] = y bits at even positions. */
static uint32_t tx_tab[1024];
static uint32_t ty_tab[1024];
static int tabs_ready;

static void init_morton_tabs(void)
{
    for (unsigned i = 0; i < 1024; i++) {
        uint32_t xs = 0, ys = 0;
        for (unsigned b = 0; b < 10; b++) {
            xs |= (uint32_t)((i >> b) & 1u) << (2 * b + 1);
            ys |= (uint32_t)((i >> b) & 1u) << (2 * b);
        }
        tx_tab[i] = xs;
        ty_tab[i] = ys;
    }
    tabs_ready = 1;
}

static int is_pow2_1024(unsigned n)
{
    return n >= 1 && n <= 1024 && (n & (n - 1)) == 0;
}
#endif

#if !WINGS64_FAST_TEXCONV
void texconv_twiddle16(const uint16_t *src, uint16_t *dst, unsigned w, unsigned h)
{
    if (w == 0 || h == 0)
        return;
    unsigned s = w < h ? w : h;
    unsigned blocks = w > h ? w / s : h / s;
    for (unsigned blk = 0; blk < blocks; blk++) {
        for (unsigned ly = 0; ly < s; ly++) {
            for (unsigned lx = 0; lx < s; lx++) {
                unsigned x = w >= h ? blk * s + lx : lx;
                unsigned y = w >= h ? ly : blk * s + ly;
                dst[blk * s * s + morton(lx, ly, s)] = src[y * w + x];
            }
        }
    }
}
#else
/* Per-texel morton twiddle, used for sizes the tables do not cover (non-pow2 or > 1024). */
static void twiddle16_slow(const uint16_t *src, uint16_t *dst, unsigned w, unsigned h)
{
    unsigned s = w < h ? w : h;
    unsigned blocks = w > h ? w / s : h / s;
    for (unsigned blk = 0; blk < blocks; blk++) {
        for (unsigned ly = 0; ly < s; ly++) {
            for (unsigned lx = 0; lx < s; lx++) {
                unsigned x = w >= h ? blk * s + lx : lx;
                unsigned y = w >= h ? ly : blk * s + ly;
                dst[blk * s * s + morton(lx, ly, s)] = src[y * w + x];
            }
        }
    }
}

void texconv_twiddle16(const uint16_t *src, uint16_t *dst, unsigned w, unsigned h)
{
    if (w == 0 || h == 0)
        return;
    if (!is_pow2_1024(w) || !is_pow2_1024(h)) {
        twiddle16_slow(src, dst, w, h);
        return;
    }
    if (!tabs_ready)
        init_morton_tabs();
    unsigned s = w < h ? w : h;
    unsigned blocks = w > h ? w / s : h / s;
    for (unsigned blk = 0; blk < blocks; blk++) {
        for (unsigned ly = 0; ly < s; ly++) {
            unsigned y = w >= h ? ly : blk * s + ly;
            unsigned x0 = w >= h ? blk * s : 0;
            const uint16_t *srow = src + y * w + x0;
            /* morton(lx, ly) = tx[lx] | ty[ly]; the two bit sets are disjoint, so the OR is an add. */
            uint16_t *drow = dst + blk * s * s + ty_tab[ly];
            for (unsigned lx = 0; lx < s; lx++)
                drow[tx_tab[lx]] = srow[lx];
        }
    }
}
#endif

#if !WINGS64_FAST_TEXCONV
void texconv_resample_pow2(const uint16_t *src, unsigned w, unsigned h, uint16_t *dst, unsigned pw, unsigned ph)
{
    if (w == 0 || h == 0 || pw == 0 || ph == 0)
        return;
    for (unsigned y = 0; y < ph; y++)
        for (unsigned x = 0; x < pw; x++)
            dst[y * pw + x] = src[(y * h / ph) * w + (x * w / pw)];
}
#else
void texconv_resample_pow2(const uint16_t *src, unsigned w, unsigned h, uint16_t *dst, unsigned pw, unsigned ph)
{
    if (w == 0 || h == 0 || pw == 0 || ph == 0)
        return;
    if (pw > 1024) {
        for (unsigned y = 0; y < ph; y++)
            for (unsigned x = 0; x < pw; x++)
                dst[y * pw + x] = src[(y * h / ph) * w + (x * w / pw)];
        return;
    }
    /* Source column per destination column, computed once. */
    unsigned xs[1024];
    for (unsigned x = 0; x < pw; x++)
        xs[x] = x * w / pw;
    for (unsigned y = 0; y < ph; y++) {
        const uint16_t *srow = src + (y * h / ph) * w;
        uint16_t *d = dst + y * pw;
        for (unsigned x = 0; x < pw; x++)
            d[x] = srow[xs[x]];
    }
}
#endif
