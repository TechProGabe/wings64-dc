/* gfx_texconv.c - N64 texture -> Dreamcast PVR texel conversion. Pure C, no KOS calls. */
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
