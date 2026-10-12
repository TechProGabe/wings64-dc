/* gfx_texconv.h - N64 texture -> Dreamcast PVR texel conversion. Pure C, no KOS calls. */
#ifndef GFX_TEXCONV_H
#define GFX_TEXCONV_H

#include <stdint.h>

typedef enum { N64FMT_RGBA = 0, N64FMT_YUV = 1, N64FMT_CI = 2, N64FMT_IA = 3, N64FMT_I = 4 } n64_fmt_t; /* G_IM_FMT_* */
typedef enum { N64SIZ_4b = 0, N64SIZ_8b = 1, N64SIZ_16b = 2, N64SIZ_32b = 3 } n64_siz_t;            /* G_IM_SIZ_* */
typedef enum { PVRFMT_ARGB1555, PVRFMT_ARGB4444, PVRFMT_UNSUPPORTED } texconv_pvrfmt_t;

/* PVR format that (fmt, siz) converts to, or PVRFMT_UNSUPPORTED (YUV, CI and unlisted combos). */
texconv_pvrfmt_t texconv_target(n64_fmt_t fmt, n64_siz_t siz);

/* TMEM line size in bytes for a row of `width` texels: width*bpp/8 rounded up to a multiple of 8. */
unsigned texconv_line_bytes(unsigned width, n64_siz_t siz);

/* Undo the TMEM swizzle in place: on odd rows, swap the two 32-bit halves of each 8-byte group.
   Self-inverse. `line_bytes` must be a multiple of 8. */
void texconv_deswizzle(uint8_t *data, unsigned line_bytes, unsigned rows);
/* General LOADBLOCK odd-line swap for any dxt (see gfx_texconv.c). */
void texconv_deswizzle_dxt(uint8_t *data, unsigned line_bytes, unsigned rows, unsigned dxt);

/* Next power of two >= n, clamped to [8, 1024]. */
unsigned texconv_pow2(unsigned n);

/* Convert w x h texels from linear, already-deswizzled `src` (row stride line_bytes, big-endian N64 data)
   into dst, a linear dst_w x dst_h u16 array. Texels beyond w/h are clamp-padded (repeat last column/row).
   `i_alpha_from_intensity` (nonzero) makes I4/I8 texels use intensity as alpha; zero gives opaque alpha.
   Returns the PVR format written, or PVRFMT_UNSUPPORTED (unsupported format, zero size, dst too small,
   or line_bytes too short for w). Requires dst_w >= w and dst_h >= h. */
texconv_pvrfmt_t texconv_convert(const uint8_t *src, unsigned line_bytes, unsigned w, unsigned h,
                                 n64_fmt_t fmt, n64_siz_t siz, int i_alpha_from_intensity,
                                 uint16_t *dst, unsigned dst_w, unsigned dst_h);

/* PVR twiddle (Morton order) for pow2 w x h. Square blocks of min(w,h) are twiddled (y bit in the lower
   position) and laid out linearly along the longer axis. src and dst are w*h u16 each; must not overlap. */
void texconv_twiddle16(const uint16_t *src, uint16_t *dst, unsigned w, unsigned h);

/* Nearest-neighbour resample of w x h u16 to pw x ph: dst[y*pw+x] = src[(y*h/ph)*w + x*w/pw].
   Used to stretch a texture to a pow2 size. src and dst must not overlap. */
void texconv_resample_pow2(const uint16_t *src, unsigned w, unsigned h, uint16_t *dst, unsigned pw, unsigned ph);

#endif /* GFX_TEXCONV_H */
