/* gfx_f3d.c — Fast3D (F3D GBI) display-list interpreter onto the PowerVR (PLAN D4 phase A).
 *
 * Runs synchronously on the scheduler thread inside osSpTaskStartGo (src/dc/ultra/os_shim.c); one
 * gfx task = one PVR scene. Spec: Birdman64 docs/notes/gfx.md (32 opcodes, encodings, quirks) and
 * src/dc/gfx/f3d_gbi.h. All matrix and vector math goes through sh4zam (docs/sh4zam-mandate.md).
 * This TU owns XMTRX while a display list runs.
 *
 * Conventions: N64 row-vector matrices have the same memory layout as sh4zam column-major ones, so
 * N64 "A x B" is sh4zam load(B), apply(A). Screen = N64 320x240 viewport mapping, scaled x2 to 640x480.
 * PVR depth = 1/w with GEQUAL (bigger is nearer).
 */
#include "gfx_pvr.h"
#include "gfx_texconv.h"
#include "f3d_gbi.h"
#include "../db.h"

#include <kos.h>
#include <string.h>
#include <sh4zam/shz_sh4zam.h>

/* DB_PERF-style phase accounting (microseconds, emulated or real). */
static uint64_t sT_vtx, sT_state, sT_conv, sT_emit, sT_other, sT_fit, sT_tex, sT_set;
/* Per-phase interpreter timers read the TMU several times per triangle; that costs real frame time,
   so they only exist in a -DDB_GFX_PROF=1 build. Frame-level timing (gfx: wait/dl/finish) stays on. */
#ifndef DB_GFX_PROF
#define DB_GFX_PROF 0
#endif
#if DB_GFX_PROF
#define TNOW() timer_us_gettime64()
#else
#define TNOW() ((uint64_t)0)
#endif

#if DB_GFX_TRACE
static int sTrace;
static uint32_t sListTris[3], sRejTrivial, sRejCull, sClipped, sVtxLoads;
static uint32_t sStEmit, sStRej[5], sStCull, sStClip;   /* per draw state: rejects by plane x+,x-,y+,y-,near */
static int sStDump;   /* per draw state: clipped polygons dumped so far */
static float sMinW, sMaxW;
#define TRACE(...) do { if (sTrace) db_log(__VA_ARGS__); } while (0)
#else
#define TRACE(...) ((void)0)
#endif

#define SCALE 2.0f                /* N64 320x240 -> 640x480 */
#ifndef MTX_DEPTH
#define MTX_DEPTH 11              /* gfx.md: 10 pushes + current */
#endif
#define DL_DEPTH 18
#ifndef GUARD
#define GUARD 4.0f   /* guard band: x/y clipped at +-GUARD*w so screen coordinates stay within the PVR's range */
#endif
/* docs/fps-plan.md change 5. WINGS64_OUTCODES: plane tests once per vertex in do_vtx, AND/OR per triangle.
   WINGS64_MVP_LAZY: XMTRX = P * MV is reloaded only after a matrix change, not on every G_VTX (only do_mtx
   and do_vtx touch XMTRX on this thread; KOS saves the XF bank on context switches). */
/* docs/fps-plan.md change 2 (I-cache layout; payoff on hardware only: the SH-4 I-cache is 8 KB direct-mapped
   and the per-triangle path aliased itself). HOT functions share one section, so they link contiguously;
   rare paths (clipping, lighting, texture uploads, rectangles, matrices) are kept out of line.
   Kill switch: -DWINGS64_HOT_LAYOUT=0. */
#ifndef WINGS64_HOT_LAYOUT
#define WINGS64_HOT_LAYOUT 1
#endif
#if WINGS64_HOT_LAYOUT
#define HOT __attribute__((section(".text.hot.gfx")))
#define COLD __attribute__((noinline, cold))
#define OUTLINE __attribute__((noinline))
#else
#define HOT
#define COLD
#define OUTLINE
#endif

#ifndef WINGS64_OUTCODES
#define WINGS64_OUTCODES 1
#endif
#ifndef WINGS64_MVP_LAZY
#define WINGS64_MVP_LAZY 1
#endif

/* ------------------------------------------------------------------ state */

typedef struct {
    float x, y, z, w;             /* clip space */
    float s, t;                   /* texel coords, s10.5 scaled by gSPTexture, /32 (texels) */
    float r, g, b, a;             /* shade 0..1 (a = vertex alpha) */
    float fog;                    /* 0..255 */
    /* --- not interpolated by the clipper (see lerp_vtx) --- */
    uint32_t oc;                  /* outcode (do_vtx): bits 0-4 beyond x+ x- y+ y- near, bit 5 needs clipping */
    uint32_t pv_epoch;            /* draw-state epoch pv was computed for (0 = stale) */
    pvr_vertex_t pv;              /* final PVR vertex for the current state */
} vtx_t;
#define VTX_LERP_FLOATS 11

typedef struct {
    uint8_t fmt, siz, cms, cmt, masks, maskt, shifts, shiftt;
    uint16_t line, tmem;
    uint16_t uls, ult, lrs, lrt;  /* 10.2 */
} tile_t;

typedef struct { uint32_t addr; uint32_t bytes; uint16_t dxt; uint8_t valid; } tmemload_t;

static struct {
    uint32_t seg[16];
    shz_mat4x4_t proj __attribute__((aligned(8)));
    shz_mat4x4_t mv[MTX_DEPTH] __attribute__((aligned(8)));
    int mv_top;
    int mvp_dirty, lights_dirty;
    float vp_sx, vp_sy, vp_tx, vp_ty;   /* pixels at 320x240 */
    uint32_t geom, omh, oml;
    uint32_t cc0, cc1;
    float prim[4], env[4], fogc[4], blend[4];
    uint32_t prim_raw, env_raw;           /* RGBA8888 as sent: combiner-fit cache key */
    uint32_t fill;
    int16_t fog_m, fog_o;
    int tex_on, tex_tile;
    float tex_ss, tex_st;
    tile_t tile[8];
    tmemload_t loads[64];               /* indexed by tmem >> 3 (64-bit word / 8) */
    uint8_t timg_fmt, timg_siz;
    uint16_t timg_w;
    uint32_t timg_addr;
    uint32_t cimg, zimg;
    int num_lights;
    uint8_t light_col[9][3];
    int8_t light_dir[9][3];
    shz_vec3_t light_obj[8];
    int8_t lookat[2][3];
    shz_vec3_t lookat_obj[2];
    vtx_t vc[16];
    uint32_t half1, half2;
} R;

/* ------------------------------------------------------------- addressing */

/* gfx.md: the game only ever sets segment 0 = 0, and passes K0 or physical addresses. On the DC
   physical RAM is 0x0Cxxxxxx, which looks like segment 12; anything whose segment has no base is
   therefore taken as physical and mapped to P1 (cached). */
static inline void *ram(uint32_t a) {
    uint32_t s;
    if (a & 0x80000000u) return (void *)(a & ~0x20000000u);
    s = (a >> 24) & 0x0F;
    if (R.seg[s]) a = R.seg[s] + (a & 0x00FFFFFFu);
    return (void *)((a & 0x1FFFFFFFu) | 0x80000000u);
}

/* --------------------------------------------------------------- matrices */

/* N64 Mtx = Mtx_u { u16 i[4][4]; u16 f[4][4]; } (integer halves, then fraction halves). The game writes
   them as u16 arrays (uvMat4CopyF2L) and ROM matrices are swapped per u16 (patches/013 "32h"), so in
   SH-4 memory element e is halfword e, not "high half of word e/2" as on the big-endian N64. */
static void mtx_from_n64(shz_mat4x4_t *out, const void *m) {
    const int16_t *ip = (const int16_t *)m;
    const uint16_t *fp = (const uint16_t *)m + 16;
    for (int e = 0; e < 16; e++)
        out->elem[e] = (float)(((int32_t)ip[e] << 16) | (int32_t)fp[e]) * (1.0f / 65536.0f);
}

static OUTLINE void do_mtx(uint32_t w0, uint32_t w1) {
    shz_mat4x4_t m __attribute__((aligned(8))), tmp __attribute__((aligned(8)));
    uint8_t p = (w0 >> 16) & 0xFF;
    mtx_from_n64(&m, ram(w1));
#if DB_GFX_TRACE
    if (sTrace && (p & F3D_G_MTX_PROJECTION))
        db_log("  proj %s %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f\n",
               (p & F3D_G_MTX_LOAD) ? "load" : "mul", m.elem[0], m.elem[1], m.elem[2], m.elem[3], m.elem[4], m.elem[5],
               m.elem[6], m.elem[7], m.elem[8], m.elem[9], m.elem[10], m.elem[11], m.elem[12], m.elem[13], m.elem[14], m.elem[15]);
#endif
    if (p & F3D_G_MTX_PROJECTION) {
        if (p & F3D_G_MTX_LOAD) shz_mat4x4_copy(&R.proj, &m);
        else { shz_xmtrx_load_apply_store_4x4(&tmp, &R.proj, &m); shz_mat4x4_copy(&R.proj, &tmp); }
    } else {
        if ((p & F3D_G_MTX_PUSH) && R.mv_top < MTX_DEPTH - 1) {
            shz_mat4x4_copy(&R.mv[R.mv_top + 1], &R.mv[R.mv_top]);
            R.mv_top++;
        }
        if (p & F3D_G_MTX_LOAD) shz_mat4x4_copy(&R.mv[R.mv_top], &m);
        else { shz_xmtrx_load_apply_store_4x4(&tmp, &R.mv[R.mv_top], &m); shz_mat4x4_copy(&R.mv[R.mv_top], &tmp); }
        R.lights_dirty = 1;
    }
    R.mvp_dirty = 1;
}

/* --------------------------------------------------------------- vertices */

/* Lighting and spherical texgen (rare in PW64: G_LIGHTING only with G_TEXTURE_GEN + DECALRGB, gfx.md). */
static COLD void vtx_lit(vtx_t *v, const uint8_t *cn) {
        /* Eye-space normal (as Birdman64's pw64-gfx): PW64 never sends lookat vectors or light
           counts; G_LIGHTING only appears with G_TEXTURE_GEN + DECALRGB (gfx.md). */
        shz_vec3_t nrm = shz_vec3_init((int8_t)cn[0], (int8_t)cn[1], (int8_t)cn[2]);
        shz_vec3_t nv = shz_vec3_normalize_safe(shz_mat4x4_transform_vec3(&R.mv[R.mv_top], nrm));
        if (R.geom & F3D_G_LIGHTING) {
            int amb = R.num_lights;
            float r = R.light_col[amb][0], g = R.light_col[amb][1], b = R.light_col[amb][2];
            for (int l = 0; l < R.num_lights && l < 8; l++) {
                shz_vec3_t ld = shz_vec3_normalize_safe(shz_vec3_init(R.light_dir[l][0], R.light_dir[l][1], R.light_dir[l][2]));
                float d = shz_vec3_dot(nv, ld);
                if (d > 0.0f) { r += d * R.light_col[l][0]; g += d * R.light_col[l][1]; b += d * R.light_col[l][2]; }
            }
            v->r = r > 255.0f ? 1.0f : r * (1.0f / 255.0f);
            v->g = g > 255.0f ? 1.0f : g * (1.0f / 255.0f);
            v->b = b > 255.0f ? 1.0f : b * (1.0f / 255.0f);
        } else {
            v->r = cn[0] * (1.0f / 255.0f); v->g = cn[1] * (1.0f / 255.0f); v->b = cn[2] * (1.0f / 255.0f);
        }
        if (R.geom & F3D_G_TEXTURE_GEN) {
            /* Spherical env map: (n_eye + 1) / 4 of the texture scale, s10.5 -> texels. */
            v->s = (nv.x + 1.0f) * 0.25f * R.tex_ss * (1.0f / 32.0f);
            v->t = (nv.y + 1.0f) * 0.25f * R.tex_st * (1.0f / 32.0f);
        }
}

static HOT void do_vtx(uint32_t w0, uint32_t w1) {
    uint32_t n, v0, len, addr;
    const uint8_t *src;
    f3d_vtx_decode(w0, w1, &n, &v0, &len, &addr);
    src = ram(addr);
    /* Lights and texgen use eye-space normals per vertex (do_vtx); no per-matrix light update. */
    if (!WINGS64_MVP_LAZY || R.mvp_dirty) {
        shz_xmtrx_load_apply_4x4(&R.proj, &R.mv[R.mv_top]);   /* XMTRX = P * MV (N64: MV x P) */
        R.mvp_dirty = 0;
    }
    for (uint32_t i = 0; i < n && v0 + i < 16; i++, src += 16) {
        const int16_t *ob = (const int16_t *)src;
        const int16_t *tc = (const int16_t *)(src + 8);
        const uint8_t *cn = src + 12;
        vtx_t *v = &R.vc[v0 + i];
        shz_vec4_t c = shz_xmtrx_transform_vec4(shz_vec4_init(ob[0], ob[1], ob[2], 1.0f));
        v->x = c.x; v->y = c.y; v->z = c.z; v->w = c.w;
        v->pv_epoch = 0;
#if WINGS64_OUTCODES
        {
            float gw = GUARD * c.w;
            uint32_t o = 0;
            if (c.x > c.w) o |= 1;
            if (c.x < -c.w) o |= 2;
            if (c.y > c.w) o |= 4;
            if (c.y < -c.w) o |= 8;
            if (c.z < -c.w) o |= 16;
            if (c.z + c.w < 0.0f || c.x > gw || c.x < -gw || c.y > gw || c.y < -gw) o |= 32;
            v->oc = o;
        }
#endif
#if DB_GFX_TRACE
        sVtxLoads++;
        if (c.w < sMinW) sMinW = c.w;
        if (c.w > sMaxW) sMaxW = c.w;
#endif
        if (R.geom & (F3D_G_LIGHTING | F3D_G_TEXTURE_GEN)) {
            vtx_lit(v, cn);
        } else {
            v->r = cn[0] * (1.0f / 255.0f); v->g = cn[1] * (1.0f / 255.0f); v->b = cn[2] * (1.0f / 255.0f);
        }
        v->a = cn[3] * (1.0f / 255.0f);
        if (!(R.geom & F3D_G_TEXTURE_GEN)) {
            v->s = tc[0] * R.tex_ss * (1.0f / 65536.0f) * (1.0f / 32.0f);
            v->t = tc[1] * R.tex_st * (1.0f / 65536.0f) * (1.0f / 32.0f);
        }
        if ((R.geom & F3D_G_FOG) && c.w > 0.0f) {
            float f = c.z * shz_invf(c.w) * R.fog_m + R.fog_o;
            v->fog = f < 0.0f ? 0.0f : f > 255.0f ? 255.0f : f;
        } else {
            v->fog = 0.0f;
        }
    }
}

/* --------------------------------------------------------------- combiner */

enum { SRC_COMB, SRC_T, SRC_SHADE, SRC_PRIM, SRC_ENV, SRC_1, SRC_0,
       SRC_COMB_A, SRC_T_A, SRC_SHADE_A, SRC_PRIM_A, SRC_ENV_A, SRC_LOD };

static int mux_rgb(int slot, int v) {        /* slot 0=a 1=b 2=c 3=d */
    switch (v) {
    case 0: return SRC_COMB;
    case 1: case 2: return SRC_T;               /* TEXEL1 -> TEXEL0 (no LOD/detail modes, gfx.md) */
    case 3: return SRC_PRIM;
    case 4: return SRC_SHADE;
    case 5: return SRC_ENV;
    }
    if (slot == 0) return v == 6 ? SRC_1 : SRC_0;
    if (slot == 1) return SRC_0;
    if (slot == 3) return v == 6 ? SRC_1 : SRC_0;
    switch (v) {                                 /* slot c */
    case 7: return SRC_COMB_A;
    case 8: case 9: return SRC_T_A;
    case 10: return SRC_PRIM_A;
    case 11: return SRC_SHADE_A;
    case 12: return SRC_ENV_A;
    case 13: return SRC_LOD;
    case 14: return SRC_LOD;                     /* PRIM_LOD_FRAC: approximated as 0 like LOD */
    }
    return SRC_0;
}
static int mux_a(int slot, int v) {
    if (slot == 2) {
        switch (v) { case 1: case 2: return SRC_T; case 3: return SRC_PRIM; case 4: return SRC_SHADE;
                     case 5: return SRC_ENV; case 0: case 6: return SRC_LOD; }
        return SRC_0;
    }
    switch (v) { case 0: return SRC_COMB; case 1: case 2: return SRC_T; case 3: return SRC_PRIM;
                 case 4: return SRC_SHADE; case 5: return SRC_ENV; case 6: return SRC_1; }
    return SRC_0;
}

/* A value is texel * m + k (per channel). */
typedef struct { float m, k; } lin_t;

static lin_t src_val(int s, int ch, const vtx_t *v, lin_t comb, lin_t comb_a, int fogging) {
    lin_t r = { 0.0f, 0.0f };
    float shade_a = fogging ? 1.0f : v->a;
    switch (s) {
    case SRC_COMB: return comb;
    case SRC_COMB_A: return comb_a;
    case SRC_T: case SRC_T_A: r.m = 1.0f; return r;
    case SRC_SHADE: r.k = ch == 0 ? v->r : ch == 1 ? v->g : ch == 2 ? v->b : shade_a; return r;
    case SRC_SHADE_A: r.k = shade_a; return r;
    case SRC_PRIM: r.k = R.prim[ch]; return r;
    case SRC_PRIM_A: r.k = R.prim[3]; return r;
    case SRC_ENV: r.k = R.env[ch]; return r;
    case SRC_ENV_A: r.k = R.env[3]; return r;
    case SRC_1: r.k = 1.0f; return r;
    default: return r;                           /* 0, LOD fraction */
    }
}

/* (a - b) * c + d on texel-linear values. A texel*texel term (e.g. TEXEL0*TEXEL1, the water and
   detail combiners) cannot be expressed per vertex; with TEXEL1 = TEXEL0 it is approximated T*T ~ T. */
static lin_t eval(lin_t a, lin_t b, lin_t c, lin_t d) {
    lin_t ab = { a.m - b.m, a.k - b.k }, r;
    r.m = ab.m * c.m + ab.m * c.k + ab.k * c.m;
    r.k = ab.k * c.k;
    r.m += d.m; r.k += d.k;
    return r;
}

/* Per-state linear model of the combiner (cc_fit): for channel ch (r,g,b,a),
     m = m0 + mS * shade_ch + mA * shade_a,  k = k0 + kS * shade_ch + kA * shade_a
   where the result is texel * m + k. Exact unless the combiner multiplies shade by shade alpha. */
static struct { int linear; float m0[4], mS[4], mA[4], k0[4], kS[4], kA[4]; } sFit;

/* Combiner compiled to source ids once per draw state (cc_compile), evaluated per vertex. */
static struct { int cycles; uint8_t rgb[2][4], a[2][4]; } sCC;

static void cc_compile(int two_cycle) {
    uint32_t w0 = R.cc0, w1 = R.cc1;
    sCC.cycles = two_cycle ? 2 : 1;
    for (int cy = 0; cy < 2; cy++) {
        sCC.rgb[cy][0] = mux_rgb(0, cy ? f3d_cc_a1(w0, w1) : f3d_cc_a0(w0, w1));
        sCC.rgb[cy][1] = mux_rgb(1, cy ? f3d_cc_b1(w0, w1) : f3d_cc_b0(w0, w1));
        sCC.rgb[cy][2] = mux_rgb(2, cy ? f3d_cc_c1(w0, w1) : f3d_cc_c0(w0, w1));
        sCC.rgb[cy][3] = mux_rgb(3, cy ? f3d_cc_d1(w0, w1) : f3d_cc_d0(w0, w1));
        sCC.a[cy][0] = mux_a(0, cy ? f3d_cc_Aa1(w0, w1) : f3d_cc_Aa0(w0, w1));
        sCC.a[cy][1] = mux_a(1, cy ? f3d_cc_Ab1(w0, w1) : f3d_cc_Ab0(w0, w1));
        sCC.a[cy][2] = mux_a(2, cy ? f3d_cc_Ac1(w0, w1) : f3d_cc_Ac0(w0, w1));
        sCC.a[cy][3] = mux_a(3, cy ? f3d_cc_Ad1(w0, w1) : f3d_cc_Ad0(w0, w1));
    }
}

static void combine(const vtx_t *v, int fogging, lin_t out[4]);

static void cc_fit(int fogging) {
    vtx_t p = { 0 };
    lin_t z[4], s1[4], a1[4], b1[4];
    combine(&p, fogging, z);
    p.r = p.g = p.b = 1.0f; combine(&p, fogging, s1);
    p.r = p.g = p.b = 0.0f; p.a = 1.0f; combine(&p, fogging, a1);
    p.r = p.g = p.b = 1.0f; combine(&p, fogging, b1);
    sFit.linear = 1;
    for (int ch = 0; ch < 4; ch++) {
        sFit.m0[ch] = z[ch].m; sFit.k0[ch] = z[ch].k;
        sFit.mA[ch] = a1[ch].m - z[ch].m; sFit.kA[ch] = a1[ch].k - z[ch].k;
        if (ch == 3) { sFit.mS[ch] = sFit.kS[ch] = 0.0f; }   /* alpha only sees shade alpha */
        else { sFit.mS[ch] = s1[ch].m - z[ch].m; sFit.kS[ch] = s1[ch].k - z[ch].k; }
        {
            float pm = sFit.m0[ch] + sFit.mS[ch] + sFit.mA[ch], pk = sFit.k0[ch] + sFit.kS[ch] + sFit.kA[ch];
            float dm = pm - b1[ch].m, dk = pk - b1[ch].k;
            if (dm > 1e-4f || dm < -1e-4f || dk > 1e-4f || dk < -1e-4f) sFit.linear = 0;
        }
    }
}

/* Evaluate the compiled combiner for one vertex: out[ch] = texel*m + k for ch = r,g,b,a. */
static COLD void combine(const vtx_t *v, int fogging, lin_t out[4]) {
    lin_t comb[4] = { { 0, 0 } }, prev[4];
    for (int cy = 0; cy < sCC.cycles; cy++) {
        const uint8_t *s = sCC.rgb[cy], *sa = sCC.a[cy];
        memcpy(prev, comb, sizeof prev);
        for (int ch = 0; ch < 3; ch++)
            comb[ch] = eval(src_val(s[0], ch, v, prev[ch], prev[3], fogging), src_val(s[1], ch, v, prev[ch], prev[3], fogging),
                            src_val(s[2], ch, v, prev[ch], prev[3], fogging), src_val(s[3], ch, v, prev[ch], prev[3], fogging));
        comb[3] = eval(src_val(sa[0], 3, v, prev[3], prev[3], fogging), src_val(sa[1], 3, v, prev[3], prev[3], fogging),
                       src_val(sa[2], 3, v, prev[3], prev[3], fogging), src_val(sa[3], 3, v, prev[3], prev[3], fogging));
    }
    memcpy(out, comb, sizeof comb);
}

/* Combiner compile + fit depend only on (cc0, cc1, cycle type, fog, prim, env): cache them.
   Kill switch: -DWINGS64_CC_CACHE=0 recomputes on every draw-state change. */
#ifndef WINGS64_STATE_SKIP
#define WINGS64_STATE_SKIP 1
#endif
#ifndef WINGS64_CC_CACHE
#define WINGS64_CC_CACHE 1
#endif
#if WINGS64_CC_CACHE
#define CC_CACHE_SLOTS 128
static struct {
    uint32_t cc0, cc1, prim, env, flags;   /* flags bit0 two_cycle, bit1 fog, bit31 valid */
    __typeof__(sCC) cc;
    __typeof__(sFit) fit;
} sCcCache[CC_CACHE_SLOTS];
uint32_t gfx_cc_cache_hits, gfx_cc_cache_misses;
#endif

static void cc_prepare(int two_cycle, int fogging) {
#if WINGS64_CC_CACHE
    uint32_t flags = 0x80000000u | (two_cycle ? 1u : 0u) | (fogging ? 2u : 0u);
    uint32_t h = R.cc0 * 0x9E3779B1u ^ R.cc1 * 0x85EBCA77u ^ R.prim_raw * 0xC2B2AE3Du ^ R.env_raw * 0x27D4EB2Fu ^ flags;
    unsigned slot = (h ^ (h >> 15)) & (CC_CACHE_SLOTS - 1);
    if (sCcCache[slot].flags == flags && sCcCache[slot].cc0 == R.cc0 && sCcCache[slot].cc1 == R.cc1 &&
        sCcCache[slot].prim == R.prim_raw && sCcCache[slot].env == R.env_raw) {
        sCC = sCcCache[slot].cc;
        sFit = sCcCache[slot].fit;
        gfx_cc_cache_hits++;
        return;
    }
    gfx_cc_cache_misses++;
#endif
    cc_compile(two_cycle);
    cc_fit(fogging);
#if WINGS64_CC_CACHE
    sCcCache[slot].cc0 = R.cc0; sCcCache[slot].cc1 = R.cc1; sCcCache[slot].prim = R.prim_raw;
    sCcCache[slot].env = R.env_raw; sCcCache[slot].flags = flags;
    sCcCache[slot].cc = sCC; sCcCache[slot].fit = sFit;
#endif
}

/* float -> 0..255 via a signed conversion (ftrc); an unsigned cast calls into libgcc on SH-4. */
static inline uint32_t clamp8(float f) { int i = (int)(f * 255.0f); return i < 0 ? 0u : i > 255 ? 255u : (uint32_t)i; }



/* ----------------------------------------------------------------- textures */

/* Bumped by every command that changes draw state; triangles reuse the computed state until then. */
static uint32_t sEpoch = 1, sStateEpoch;
static gpvr_state_t sState;
#define DIRTY() (sEpoch++)

static gpvr_tex_t *cur_tex;
static float cur_tex_w, cur_tex_h, cur_uls, cur_ult;
static int cur_shift_s, cur_shift_t;
static float cur_u_scale, cur_u_off, cur_v_scale, cur_v_off;

static unsigned sTexEffW, sTexEffH;
static const char *sTexMiss;          /* why the last texture_for_tile returned NULL (trace) */   /* texel size the last texture_for_tile result was built from */

static uint32_t sample_hash(const uint8_t *p, uint32_t bytes) {
    uint32_t h = 2166136261u, step = bytes > 64 ? bytes / 16 : 4;
    for (uint32_t i = 0; i + 4 <= bytes; i += step) { h ^= *(const uint32_t *)(p + (i & ~3u)); h *= 16777619u; }
    return h ^ bytes;
}

/* docs/fps-plan.md change 3: draw_state runs ~136 times a frame and each textured call hashed up to 4 KB of
   texture RAM (16 strided loads) and walked the cache. Within one display list the app thread cannot run
   (lower priority), so a (load address, format/size key) seen this frame maps to the same texture.
   Kill switch: -DWINGS64_TEX_MEMO=0. */
#ifndef WINGS64_TEX_MEMO
#define WINGS64_TEX_MEMO 1
#endif
/* LOADBLOCK dxt that does not match the tile's line: exact RDP odd-line swap (gfx_texconv.c).
   Kill switch -DWINGS64_DXT_EXACT=0. */
#ifndef WINGS64_DXT_EXACT
#define WINGS64_DXT_EXACT 1
#endif
#if WINGS64_TEX_MEMO
#define TEX_MEMO_SLOTS 64
static struct { uint32_t frame, addr, fmtkey; gpvr_tex_t *gt; uint16_t w, h; } sTexMemo[TEX_MEMO_SLOTS];
static uint32_t sTexFrame = 1;
#endif

static COLD gpvr_tex_t *texture_for_tile(int tidx) {
    /* TMEM is 4 KB: at most 8192 texels (4-bit) per load; PVR textures go up to 1024 wide. */
    static uint8_t raw[8192] __attribute__((aligned(32)));
    static uint16_t conv[16384] __attribute__((aligned(32)));
    static uint16_t pow2buf[32768] __attribute__((aligned(32)));
    tile_t *t = &R.tile[tidx];
    tmemload_t *ld = &R.loads[(t->tmem >> 3) & 63];
    unsigned w = ((t->lrs - t->uls) >> 2) + 1, h = ((t->lrt - t->ult) >> 2) + 1;
    /* RGBA32 splits each texel across TMEM's two banks, so SETTILE's line covers half a row. */
    unsigned line = t->line * 8 * (t->siz == 3 ? 2 : 1), bytes, pw, ph;
    uint32_t fmtkey, sample;
    gpvr_tex_t *gt;
    texconv_pvrfmt_t pf;
    sTexMiss = "load/line";
    if (!ld->valid || t->line == 0) return NULL;
    if (t->masks && (1u << t->masks) < w) w = 1u << t->masks;
    if (t->maskt && (1u << t->maskt) < h) h = 1u << t->maskt;
    /* Tiles may declare more rows than were loaded (UVBT tile height, font atlases; gfx.md):
       only loaded rows exist in TMEM. */
    if (h > ld->bytes / line) h = ld->bytes / line;
    sTexMiss = "size";
    if (w == 0 || h == 0 || w > 1024 || w * h > 16384) return NULL;
    bytes = line * h;
    if (bytes > sizeof raw) return NULL;
    sTexEffW = w; sTexEffH = h;
    fmtkey = t->fmt | t->siz << 3 | (w - 1) << 5 | (h - 1) << 15 | (uint32_t)(line & 0xFF) << 24;
#if WINGS64_TEX_MEMO
    {
        static uint32_t seen_resets;   /* a cache reset (VRAM pressure) frees every entry: drop the memo */
        if (seen_resets != gpvr_stats.resets) { seen_resets = gpvr_stats.resets; sTexFrame++; }
    }
    unsigned mslot = ((ld->addr >> 3) ^ (fmtkey * 0x9E3779B1u >> 26)) & (TEX_MEMO_SLOTS - 1);
    if (sTexMemo[mslot].frame == sTexFrame && sTexMemo[mslot].addr == ld->addr && sTexMemo[mslot].fmtkey == fmtkey) {
        gpvr_stats.tex_hits++;
        return sTexMemo[mslot].gt;
    }
#endif
    sample = sample_hash(ram(ld->addr), bytes);
    gt = gpvr_tex_lookup(ld->addr, fmtkey, sample);
#if WINGS64_TEX_MEMO
    if (gt) {
        sTexMemo[mslot].frame = sTexFrame; sTexMemo[mslot].addr = ld->addr; sTexMemo[mslot].fmtkey = fmtkey;
        sTexMemo[mslot].gt = gt;
        return gt;
    }
#else
    if (gt) return gt;
#endif
    shz_memcpy(raw, ram(ld->addr), (bytes + 7) & ~7u);
    if (ld->dxt == 0) texconv_deswizzle(raw, line, bytes / line);  /* pre-swizzled data (gfx.md) */
#if WINGS64_DXT_EXACT
    else if (t->siz != 3 && ((uint32_t)ld->dxt * (line / 8) != 2048u))   /* dxt not sized for this line */
        texconv_deswizzle_dxt(raw, line, bytes / line, ld->dxt);
#endif
    pf = texconv_convert(raw, line, w, h, (n64_fmt_t)t->fmt, (n64_siz_t)t->siz, 1, conv, w, h);
    sTexMiss = "format";
    if (pf == PVRFMT_UNSUPPORTED) return NULL;
    pw = texconv_pow2(w); ph = texconv_pow2(h);
    if (pw * ph > 32768) return NULL;
#if DB_GFX_TRACE
    {
        extern int wings64_game_state(void);
        static int nlog;
        if (wings64_game_state() == 5 && nlog < 40) {
            nlog++;
            db_log("  texup fmt %d siz %d %ux%u dxt %d raw %02x%02x%02x%02x%02x%02x%02x%02x conv %04x %04x %04x %04x\n",
                   t->fmt, t->siz, w, h, ld->dxt, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
                   conv[0], conv[1], conv[w / 2], conv[(h / 2) * w + w / 2]);
        }
    }
#endif
    /* Resample (nearest) to pow2 so wrap, mirror and clamp all keep working with u = s / w.
       (gfx_texconv.c, no per-texel divide unless -DWINGS64_FAST_TEXCONV=0) */
    texconv_resample_pow2(conv, w, h, pow2buf, pw, ph);
    gt = gpvr_tex_insert(ld->addr, fmtkey, sample, pow2buf, pw, ph,
                         pf == PVRFMT_ARGB1555 ? PVR_TXRFMT_ARGB1555 : PVR_TXRFMT_ARGB4444);
#if WINGS64_TEX_MEMO
    if (gt) {
        sTexMemo[mslot].frame = sTexFrame; sTexMemo[mslot].addr = ld->addr; sTexMemo[mslot].fmtkey = fmtkey;
        sTexMemo[mslot].gt = gt;
    }
#endif
    return gt;
}

static float shift_scale(int sh) {
    if (sh == 0) return 1.0f;
    if (sh <= 10) return 1.0f / (float)(1 << sh);
    return (float)(1 << (16 - sh));
}

/* ------------------------------------------------------------- draw state */

/* Cross-list draw order. The N64 draws strictly in submission order; the PVR renders all OP, then PT, then
   TR polygons, and only depth orders them across lists. Layers drawn without a depth test therefore need
   help (hardware run: a translucent horizon band the game draws before the scene showed through the glider):
   - before the frame's first z-tested draw (sky, ground plane, horizon band, 2D menu backgrounds) each layer
     gets a tiny painter depth that grows with submission order, tested GEQUAL and written by OP/PT, so later
     layers and all 3D geometry (z = 1/w, far larger) cover earlier ones whatever their list;
   - after it (HUD, overlays) depth-ALWAYS layers go to the TR list, which keeps submission order and comes
     after every OP/PT polygon. DIVERGES: z-tested geometry drawn after an overlay ends up under it.
   Kill switch -DWINGS64_LAYER_Z=0. */
#ifndef WINGS64_LAYER_Z
#define WINGS64_LAYER_Z 1
#endif
#define LAYER_STEP 1e-10f          /* 4000 layers stay under 4e-7; scene 1/w stays above ~5e-5 */
#define LAYER_MAX 4000
static float sLayerZ;              /* >0: painter depth of the current background layer */
static int sLayerN, sSceneZ;       /* layers this frame; a z-tested draw has happened this frame */

static void layer_fix(gpvr_state_t *st) {
#if WINGS64_LAYER_Z
    sLayerZ = 0.0f;
    if (st->depth_cmp != PVR_DEPTHCMP_ALWAYS) { sSceneZ = 1; return; }
    if (!sSceneZ) {
        if (sLayerN < LAYER_MAX) sLayerN++;
        sLayerZ = LAYER_STEP * (float)sLayerN;
        st->depth_cmp = PVR_DEPTHCMP_GEQUAL;
        st->depth_write = st->list != GPVR_TR;
    } else if (st->list != GPVR_TR) {
        st->blend_src = st->list == GPVR_PT ? PVR_BLEND_SRCALPHA : PVR_BLEND_ONE;
        st->blend_dst = st->list == GPVR_PT ? PVR_BLEND_INVSRCALPHA : PVR_BLEND_ZERO;
        st->list = GPVR_TR;
    }
#else
    (void)st;
#endif
}

static OUTLINE int draw_state(int textured_possible, int depth_override_always, gpvr_state_t *st) {
    uint32_t rm = R.oml;
    int cycle = (R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3;
    int zb = (R.geom & F3D_G_ZBUFFER) && !depth_override_always;
    memset(st, 0, sizeof *st);
    { uint64_t t0 = TNOW();
    cc_prepare(cycle == 1, (R.geom & F3D_G_FOG) != 0);
    sT_fit += TNOW() - t0; }
#ifdef DB_GFX_SOLID
    /* debug: every triangle opaque, untextured, white -> screen coverage only */
    memset(st, 0, sizeof *st); st->list = GPVR_OP; st->depth_cmp = PVR_DEPTHCMP_GEQUAL; st->depth_write = 1;
    st->filter = PVR_FILTER_NEAREST; cur_tex = NULL;
    sFit.linear = 1;
    for (int ch = 0; ch < 4; ch++) { sFit.m0[ch] = sFit.mS[ch] = sFit.mA[ch] = sFit.kS[ch] = sFit.kA[ch] = 0.0f; sFit.k0[ch] = 1.0f; }
    return st->list;
#endif
    if ((rm & 0x4000) /* FORCE_BL */ && cycle < 2) {
        st->list = GPVR_TR;
        st->blend_src = PVR_BLEND_SRCALPHA;
        st->blend_dst = PVR_BLEND_INVSRCALPHA;
    } else if ((rm & 0x1000) /* CVG_X_ALPHA */ || (R.oml & 3) == 1 /* G_AC_THRESHOLD */) {
        st->list = GPVR_PT;
    } else {
        st->list = GPVR_OP;
    }
#ifdef DB_GFX_OPAQUE_ALL
    st->list = GPVR_OP;   /* debug: draw everything opaque to see geometry hidden by alpha */
#endif
    st->depth_cmp = (zb && (rm & 0x10)) ? PVR_DEPTHCMP_GEQUAL : PVR_DEPTHCMP_ALWAYS;
    st->depth_write = zb && (rm & 0x20);
    st->fog = (R.geom & F3D_G_FOG) && cycle < 2 ? 1 : 0;
    cur_tex = NULL;
    if (textured_possible && R.tex_on) {
        tile_t *t = &R.tile[R.tex_tile];
        { uint64_t t0 = TNOW(); cur_tex = texture_for_tile(R.tex_tile); sT_tex += TNOW() - t0; }
#if DB_GFX_TRACE
        if (!cur_tex && sTrace)
            db_log("  texmiss %s tile %d fmt %d siz %d line %d tmem %d s %d-%d t %d-%d mask %d/%d load %d bytes %lu dxt %d cc %08lx:%08lx\n",
                   sTexMiss, R.tex_tile, t->fmt, t->siz, t->line, t->tmem, t->uls >> 2, t->lrs >> 2, t->ult >> 2, t->lrt >> 2,
                   t->masks, t->maskt, R.loads[(t->tmem >> 3) & 63].valid, (unsigned long)R.loads[(t->tmem >> 3) & 63].bytes,
                   R.loads[(t->tmem >> 3) & 63].dxt, (unsigned long)R.cc0, (unsigned long)R.cc1);
#endif
        if (cur_tex) {
            st->tex = cur_tex->ptr;
            st->tex_fmt = cur_tex->pvr_fmt;
            st->tex_w = cur_tex->w; st->tex_h = cur_tex->h;
            st->filter = ((R.omh >> F3D_G_MDSFT_TEXTFILT) & 3) == 0 ? PVR_FILTER_NEAREST : PVR_FILTER_BILINEAR;
            st->clamp = ((t->cms & 2) ? PVR_UVCLAMP_U : 0) | ((t->cmt & 2) ? PVR_UVCLAMP_V : 0);
            st->flip = ((t->cms & 1) ? PVR_UVFLIP_U : 0) | ((t->cmt & 1) ? PVR_UVFLIP_V : 0);
            st->env = PVR_TXRENV_MODULATEALPHA;
            /* Texel alpha only counts when the combiner's alpha output uses it. A combiner with
               constant or shade alpha (sky cloud strip: alpha = 1 under CVG_X_ALPHA) must not let
               transparent texels punch holes; PVR then takes alpha from the vertex alone. */
            st->txr_alpha = (sFit.m0[3] > 1e-4f || sFit.m0[3] < -1e-4f || sFit.mA[3] > 1e-4f || sFit.mA[3] < -1e-4f);
            cur_tex_w = (float)sTexEffW;   /* the texel extent the PVR texture spans (mask/loaded-row clamped) */
            cur_tex_h = (float)sTexEffH;
            cur_uls = t->uls * 0.25f; cur_ult = t->ult * 0.25f;
            cur_shift_s = t->shifts; cur_shift_t = t->shiftt;
            cur_u_scale = shift_scale(cur_shift_s) / cur_tex_w; cur_u_off = cur_uls / cur_tex_w;
            cur_v_scale = shift_scale(cur_shift_t) / cur_tex_h; cur_v_off = cur_ult / cur_tex_h;
        }
    }
    return st->list;
}

/* --------------------------------------------------------------- triangles */

static HOT void to_pvr(const vtx_t *v, pvr_vertex_t *o, int textured, int decal) {
    lin_t c[4];
    float iw = shz_invf(v->w);
    if (sFit.linear) {
        const float sh[4] = { v->r, v->g, v->b, v->a };
        float sa = (R.geom & F3D_G_FOG) ? 1.0f : v->a;
        for (int ch = 0; ch < 4; ch++) {
            c[ch].m = sFit.m0[ch] + sFit.mS[ch] * sh[ch] + sFit.mA[ch] * sa;
            c[ch].k = sFit.k0[ch] + sFit.kS[ch] * sh[ch] + sFit.kA[ch] * sa;
        }
    } else {
        combine(v, (R.geom & F3D_G_FOG) != 0, c);
    }
    o->flags = PVR_CMD_VERTEX;
    o->x = (R.vp_tx + v->x * iw * R.vp_sx) * SCALE;
    o->y = (R.vp_ty - v->y * iw * R.vp_sy) * SCALE;
    o->z = sLayerZ > 0.0f ? sLayerZ : decal ? iw * 1.003f : iw;
    if (textured) {
        o->u = v->s * cur_u_scale - cur_u_off;   /* ((s * shift) - uls) / w, folded per draw state */
        o->v = v->t * cur_v_scale - cur_v_off;
        o->argb = (clamp8(c[3].m != 0.0f ? c[3].m : c[3].k) << 24) | (clamp8(c[0].m) << 16) | (clamp8(c[1].m) << 8) | clamp8(c[2].m);
    } else {
        o->u = o->v = 0.0f;
        o->argb = (clamp8(c[3].k) << 24) | (clamp8(c[0].k) << 16) | (clamp8(c[1].k) << 8) | clamp8(c[2].k);
    }
    o->oargb = ((uint32_t)v->fog << 24) | (textured ? (clamp8(c[0].k) << 16) | (clamp8(c[1].k) << 8) | clamp8(c[2].k) : 0);
}

static void lerp_vtx(vtx_t *o, const vtx_t *a, const vtx_t *b, float t) {
    const float *pa = &a->x, *pb = &b->x;
    float *po = &o->x;
    for (unsigned i = 0; i < VTX_LERP_FLOATS; i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
}

static void lerp_vtx(vtx_t *o, const vtx_t *a, const vtx_t *b, float t);

static COLD void emit_poly(vtx_t *poly, int n, int textured, int decal) {
    pvr_vertex_t pv[12];
    for (int i = 0; i < n; i++) to_pvr(&poly[i], &pv[i], textured, decal);
    for (int i = 1; i + 1 < n; i++) gpvr_tri_pv(&pv[0], &pv[i], &pv[i + 1]);
#if DB_GFX_TRACE
    sListTris[sState.list] += n - 2; sStEmit += n - 2;
    if (sTrace && sStDump < 6 && sState.depth_cmp == PVR_DEPTHCMP_ALWAYS) {   /* env/sky states: no Z */
        sStDump++;
        for (int i = 0; i < n; i++)
            db_log("      clip-poly v%d x %.1f y %.1f z %.3g argb %08lx oargb %08lx shade %.2f %.2f %.2f %.2f\n", i, pv[i].x, pv[i].y,
                   pv[i].z, (unsigned long)pv[i].argb, (unsigned long)pv[i].oargb, poly[i].r, poly[i].g, poly[i].b, poly[i].a);
    }
#endif
}

/* Sutherland-Hodgman in clip space against near (z + w >= 0) and the x/y guard-band planes; few
   triangles need it, so it lives out of line. */
static COLD void clip_tri(vtx_t *const v[3], int textured, int decal) {
#ifdef DB_GFX_NO_CULL
    const uint32_t cullmask = 0;
#else
    const uint32_t cullmask = F3D_G_CULL_FRONT | F3D_G_CULL_BACK;
#endif
    vtx_t bufA[12], bufB[12], *in = bufA, *out = bufB, *tmp;
    int n = 3;
#if DB_GFX_TRACE
    sClipped++; sStClip++;
#endif
    for (int i = 0; i < 3; i++) in[i] = *v[i];
    for (int plane = 0; plane < 5 && n >= 3; plane++) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            vtx_t *a = &in[i], *b = &in[(i + 1) % n];
            float da, db;
            switch (plane) {
            case 0: da = a->z + a->w; db = b->z + b->w; break;                       /* near */
            case 1: da = GUARD * a->w - a->x; db = GUARD * b->w - b->x; break;       /* x <= G w */
            case 2: da = GUARD * a->w + a->x; db = GUARD * b->w + b->x; break;       /* x >= -G w */
            case 3: da = GUARD * a->w - a->y; db = GUARD * b->w - b->y; break;       /* y <= G w */
            default: da = GUARD * a->w + a->y; db = GUARD * b->w + b->y; break;      /* y >= -G w */
            }
            if (da >= 0.0f) out[m++] = *a;
            if ((da >= 0.0f) != (db >= 0.0f) && m < 12) lerp_vtx(&out[m++], a, b, da / (da - db));
        }
        n = m; tmp = in; in = out; out = tmp;
    }
    out = in;
    if (n < 3) return;
    for (int i = 0; i < n; i++) if (out[i].w < 1e-5f) out[i].w = 1e-5f;
    if (R.geom & cullmask) {
        /* Clipped polygon: all w > 0 now, so the NDC area sign is the true orientation. */
        float area = 0.0f;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            area += (out[i].x / out[i].w) * (out[j].y / out[j].w) - (out[j].x / out[j].w) * (out[i].y / out[i].w);
        }
        /* Same convention as the unclipped path: cross = -det, i.e. -2*area here. */
        if ((R.geom & F3D_G_CULL_BACK) && -area >= 0.0f) return;
        if ((R.geom & F3D_G_CULL_FRONT) && -area <= 0.0f) return;
    }
    emit_poly(out, n, textured, decal);
}

/* ------------------------------------------------------------ shadow volumes */

/* PW64 draws vehicle shadows as z-buffer shadow volumes (kernel/graphics.c uvGfxStateDrawDL): pass 1 draws
   the volume's back faces with the colour image pointed at the z-buffer (cimg == zimg), writing "nearest"
   wherever they are visible; pass 2 draws the same display list's front faces as dark translucent shade,
   z-tested, so only terrain inside the volume darkens. The PVR has this natively: pass 1's triangles (all of
   them, culling ignored) become one OP cheap-shadow modifier volume, and pass 2's display list call is
   skipped. A volume that crosses the near plane or projects far off-screen is dropped for that frame (an
   opened volume would invert its inside test). DIVERGES: shadow intensity is one global PVR register,
   taken from the volume's vertex alpha (pass 2 blends shade with that alpha).
   Kill switch -DWINGS64_SHADOW_MOD=0 (gfx_pvr.h): both passes dropped, no shadows. */
#define VOL_MAX 256
#define VOL_COORD_MAX 16384.0f    /* screen pixels; beyond this the volume is dropped, not distorted */
static float sVol[VOL_MAX * 9];
static int sVolN, sVolBad;
static float sVolAlpha;
static int sVolAlphaN;
static uint32_t sShadowDL;        /* display list pass 1 drew: pass 2 calls it again (skipped) */
static int sShadowRec;            /* sShadowDL taken in this pass 1 (its nested calls are not the one) */

static void shadow_capture(vtx_t *const v[3]) {
#if WINGS64_SHADOW_MOD
    float *o;
    int behind = 0;
    for (int i = 0; i < 3; i++) behind += v[i]->z + v[i]->w < 0.0f || v[i]->w < 1e-4f;
    if (behind == 3) return;                   /* wholly behind the eye: covers no pixel */
    if (behind || sVolN == VOL_MAX) { sVolBad = 1; return; }
    o = &sVol[sVolN * 9];
    for (int i = 0; i < 3; i++) {
        float iw = shz_invf(v[i]->w);
        float x = (R.vp_tx + v[i]->x * iw * R.vp_sx) * SCALE, y = (R.vp_ty - v[i]->y * iw * R.vp_sy) * SCALE;
        if (x > VOL_COORD_MAX || x < -VOL_COORD_MAX || y > VOL_COORD_MAX || y < -VOL_COORD_MAX) { sVolBad = 1; return; }
        o[i * 3 + 0] = x; o[i * 3 + 1] = y; o[i * 3 + 2] = iw;
        sVolAlpha += v[i]->a;
        sVolAlphaN++;
    }
    sVolN++;
#else
    (void)v;
#endif
}

/* Pass 1 ended (colour image back on the frame buffer) or the frame ended. */
static void shadow_flush(void) {
#if DB_GFX_TRACE
    if (sVolN || sVolBad) {
        static int nlog;
        if (nlog < 12) {
            nlog++;
            db_log("  shadow volume: %d tris%s, alpha avg %.2f\n", sVolN, sVolBad ? " (dropped)" : "",
                   sVolAlphaN ? sVolAlpha / (float)sVolAlphaN : 0.0f);
            for (int i = 0; i < sVolN && i < 24; i++)
                db_log("    vt%d %.0f,%.0f,%.3g %.0f,%.0f,%.3g %.0f,%.0f,%.3g\n", i, sVol[i * 9], sVol[i * 9 + 1], sVol[i * 9 + 2],
                       sVol[i * 9 + 3], sVol[i * 9 + 4], sVol[i * 9 + 5], sVol[i * 9 + 6], sVol[i * 9 + 7], sVol[i * 9 + 8]);
        }
    }
#endif
#if WINGS64_SHADOW_MOD
    if (sVolN && !sVolBad) {
        float a = sVolAlphaN ? sVolAlpha / (float)sVolAlphaN : 0.5f;
        gpvr_mod_volume(sVol, sVolN);
        gpvr_shadow_scale(a < 0.0f ? 1.0f : a > 1.0f ? 0.0f : 1.0f - a);
    }
#endif
    sVolN = sVolBad = sVolAlphaN = sShadowRec = 0;
    sVolAlpha = 0.0f;
}

static HOT void do_tri(int ia, int ib, int ic) {
    vtx_t *v[3] = { &R.vc[ia], &R.vc[ib], &R.vc[ic] };
    int textured, decal;
    /* Colour image pointed at the z-buffer (cimg == zimg): shadow volume pass 1 (see shadow_capture). */
    if (R.cimg == R.zimg && R.cimg != 0) { shadow_capture(v); return; }
#if WINGS64_OUTCODES
    /* Trivial reject: all three beyond the same side plane (x+, x-, y+, y-, near; no far plane). */
    if (v[0]->oc & v[1]->oc & v[2]->oc & 31) {
#if DB_GFX_TRACE
        sRejTrivial++;
#endif
        return;
    }
#else
    /* Trivial reject: all three beyond the same side plane (x+, x-, y+, y-, near; no far plane). */
    {
        int plane = -1;
        if (v[0]->x > v[0]->w && v[1]->x > v[1]->w && v[2]->x > v[2]->w) plane = 0;
        else if (v[0]->x < -v[0]->w && v[1]->x < -v[1]->w && v[2]->x < -v[2]->w) plane = 1;
        else if (v[0]->y > v[0]->w && v[1]->y > v[1]->w && v[2]->y > v[2]->w) plane = 2;
        else if (v[0]->y < -v[0]->w && v[1]->y < -v[1]->w && v[2]->y < -v[2]->w) plane = 3;
        else if (v[0]->z < -v[0]->w && v[1]->z < -v[1]->w && v[2]->z < -v[2]->w) plane = 4;
        if (plane >= 0) {
#if DB_GFX_TRACE
            sRejTrivial++; sStRej[plane]++;
            if (sTrace && sStRej[plane] <= 2)
                db_log("      rej%d v0 %.1f %.1f %.1f %.1f | v1 %.1f %.1f %.1f %.1f | v2 %.1f %.1f %.1f %.1f\n", plane,
                       v[0]->x, v[0]->y, v[0]->z, v[0]->w, v[1]->x, v[1]->y, v[1]->z, v[1]->w, v[2]->x, v[2]->y, v[2]->z, v[2]->w);
#endif
            return;
        }
    }
#endif
#ifdef DB_GFX_NO_CULL
    const uint32_t cullmask = 0;   /* debug: no back-face culling */
#else
    const uint32_t cullmask = F3D_G_CULL_FRONT | F3D_G_CULL_BACK;
#endif
#if WINGS64_OUTCODES
    int needs_clip = ((v[0]->oc | v[1]->oc | v[2]->oc) & 32) != 0;
#else
    int needs_clip = 0;
    for (int i = 0; i < 3; i++) {
        float gw = GUARD * v[i]->w;
        if (v[i]->z + v[i]->w < 0.0f || v[i]->x > gw || v[i]->x < -gw || v[i]->y > gw || v[i]->y < -gw) needs_clip = 1;
    }
#endif
    /* Triangles crossing the near plane are culled after clipping (below): their orientation before
       clipping is not reliable once a vertex is behind the eye. */
    if ((R.geom & cullmask) && !needs_clip) {
        float det = v[0]->x * (v[1]->y * v[2]->w - v[2]->y * v[1]->w)
                  - v[0]->y * (v[1]->x * v[2]->w - v[2]->x * v[1]->w)
                  + v[0]->w * (v[1]->x * v[2]->y - v[2]->x * v[1]->y);
        float cross = -det;
#if DB_GFX_TRACE
        if (((R.geom & F3D_G_CULL_BACK) && cross >= 0.0f) || ((R.geom & F3D_G_CULL_FRONT) && cross <= 0.0f)) { sRejCull++; sStCull++; }
#endif
        if ((R.geom & F3D_G_CULL_BACK) && cross >= 0.0f) return;
        if ((R.geom & F3D_G_CULL_FRONT) && cross <= 0.0f) return;
    }
    { uint64_t t = TNOW();
    if (sStateEpoch != sEpoch) {
#if DB_GFX_TRACE
        if (sTrace && (sStEmit | sStRej[0] | sStRej[1] | sStRej[2] | sStRej[3] | sStRej[4] | sStCull | sStClip))
            db_log("    -> emit %lu rej x+%lu x-%lu y+%lu y-%lu near%lu cull %lu clip %lu\n", (unsigned long)sStEmit,
                   (unsigned long)sStRej[0], (unsigned long)sStRej[1], (unsigned long)sStRej[2], (unsigned long)sStRej[3],
                   (unsigned long)sStRej[4], (unsigned long)sStCull, (unsigned long)sStClip);
        sStEmit = sStRej[0] = sStRej[1] = sStRej[2] = sStRej[3] = sStRej[4] = sStCull = sStClip = 0;
        sStDump = 0;
#endif
        draw_state(1, 0, &sState); sStateEpoch = sEpoch;
        layer_fix(&sState);
#if DB_GFX_TRACE
        if (sTrace)
            db_log("  st geom %06lx cc %08lx:%08lx omh %08lx oml %08lx list %d tex %d %dx%d fit %d nl %d prim %02x%02x%02x env %02x%02x%02x\n",
                   (unsigned long)R.geom, (unsigned long)R.cc0, (unsigned long)R.cc1, (unsigned long)R.omh,
                   (unsigned long)R.oml, sState.list, sState.tex != NULL, sState.tex_w, sState.tex_h, sFit.linear,
                   R.num_lights, (int)(R.prim[0] * 255), (int)(R.prim[1] * 255), (int)(R.prim[2] * 255),
                   (int)(R.env[0] * 255), (int)(R.env[1] * 255), (int)(R.env[2] * 255));
#endif
    }
    {
        /* Skip the per-triangle header compare when nothing changed since this state was last sent.
           Kill switch: -DWINGS64_STATE_SKIP=0. */
        static uint32_t sSentSerial, sSentEpoch;
        uint64_t t0 = TNOW();
        if (!WINGS64_STATE_SKIP || sSentEpoch != sStateEpoch || sSentSerial != gpvr_state_serial) {
            gpvr_set_state(&sState);
            sSentEpoch = sStateEpoch; sSentSerial = gpvr_state_serial;
        }
        sT_set += TNOW() - t0;
    }
    sT_state += TNOW() - t; }
    textured = sState.tex != NULL;
    decal = ((R.oml >> 10) & 3) == 3;
    if (!needs_clip) {
        /* Common case: no near clip; each cache vertex converts once per draw state. */
        uint64_t t = TNOW();
        for (int i = 0; i < 3; i++)
            if (v[i]->pv_epoch != sEpoch) { to_pvr(v[i], &v[i]->pv, textured, decal); v[i]->pv_epoch = sEpoch; }
        sT_conv += TNOW() - t; t = TNOW();
        gpvr_tri_pv_inline(&v[0]->pv, &v[1]->pv, &v[2]->pv);
        sT_emit += TNOW() - t;
#if DB_GFX_TRACE
        sListTris[sState.list]++; sStEmit++;
#endif
        return;
    }
    clip_tri(v, textured, decal);
}

/* ------------------------------------------------------------- rectangles */

static void screen_quad(float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1,
                        int textured, uint32_t argb, uint32_t oargb) {
    gpvr_vtx_t q[4];
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    if (textured) {
        float iw = shz_invf(cur_tex_w), ih = shz_invf(cur_tex_h);
        u0 = (s0 - cur_uls) * iw; u1 = (s1 - cur_uls) * iw;
        v0 = (t0 - cur_ult) * ih; v1 = (t1 - cur_ult) * ih;
    }
    float z = sLayerZ > 0.0f ? sLayerZ : 1.0f;
    q[0] = (gpvr_vtx_t){ x0 * SCALE, y0 * SCALE, z, u0, v0, argb, oargb };
    q[1] = (gpvr_vtx_t){ x1 * SCALE, y0 * SCALE, z, u1, v0, argb, oargb };
    q[2] = (gpvr_vtx_t){ x0 * SCALE, y1 * SCALE, z, u0, v1, argb, oargb };
    q[3] = (gpvr_vtx_t){ x1 * SCALE, y1 * SCALE, z, u1, v1, argb, oargb };
    gpvr_tri(&q[0], &q[1], &q[2]);
    gpvr_tri(&q[1], &q[3], &q[2]);
}

/* docs/fps-plan.md change 4: menus draw every glyph as a texrect, and each one recompiled and re-evaluated
   the whole combiner. The result depends only on the combiner words, prim/env colours, cycle type and
   whether a texture is bound, so the last answer is reused while those match. Kill switch WINGS64_RECT_FIT=0. */
#ifndef WINGS64_RECT_FIT
#define WINGS64_RECT_FIT 1
#endif
static void rect_colors(int textured, uint32_t *argb, uint32_t *oargb) {
    vtx_t white = { 0 };   /* RDP rectangles have no shade: SHADE inputs read 0 */
    lin_t c[4];
    int two = ((R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3) == 1;
#if WINGS64_RECT_FIT
    static struct { uint32_t cc0, cc1, prim, env, key, argb, oargb; int valid; } last;
    uint32_t key = (uint32_t)two | (uint32_t)textured << 1;
    if (last.valid && last.cc0 == R.cc0 && last.cc1 == R.cc1 && last.prim == R.prim_raw && last.env == R.env_raw &&
        last.key == key) {
        *argb = last.argb;
        *oargb = last.oargb;
        return;
    }
#endif
    cc_compile(two);
    combine(&white, 0, c);
    if (textured) {
        *argb = (clamp8(c[3].m != 0.0f ? c[3].m : c[3].k) << 24) | (clamp8(c[0].m) << 16) | (clamp8(c[1].m) << 8) | clamp8(c[2].m);
        *oargb = (clamp8(c[0].k) << 16) | (clamp8(c[1].k) << 8) | clamp8(c[2].k);
    } else {
        *argb = (clamp8(c[3].k) << 24) | (clamp8(c[0].k) << 16) | (clamp8(c[1].k) << 8) | clamp8(c[2].k);
        *oargb = 0;
    }
#if WINGS64_RECT_FIT
    last.cc0 = R.cc0; last.cc1 = R.cc1; last.prim = R.prim_raw; last.env = R.env_raw; last.key = key;
    last.argb = *argb; last.oargb = *oargb; last.valid = 1;
#endif
}

static OUTLINE void do_fillrect(uint32_t w0, uint32_t w1) {
    uint16_t lrx, lry, ulx, uly;
    int cycle = (R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3;
    gpvr_state_t st;
    uint32_t argb, oargb;
    f3d_fillrect_decode(w0, w1, &lrx, &lry, &ulx, &uly);
    if (R.cimg == R.zimg) return;            /* z-buffer clear (gfx.md): the PVR clears depth per frame */
    draw_state(0, 1, &st);
    st.fog = 0;
    if (cycle == 3) {                        /* G_CYC_FILL: RGBA5551 fill color */
        uint16_t c = R.fill & 0xFFFF;
        argb = 0xFF000000u | (((c >> 11) & 31) * 255 / 31) << 16 | (((c >> 6) & 31) * 255 / 31) << 8 | ((c >> 1) & 31) * 255 / 31;
        oargb = 0;
        st.list = GPVR_OP;
        lrx++; lry++;                        /* fill/copy include the lower-right edge */
    } else {
        rect_colors(0, &argb, &oargb);
    }
    layer_fix(&st);
    gpvr_set_state(&st);
    TRACE("  fill %d,%d-%d,%d cyc %d list %d argb %08lx oml %08lx cimg %08lx\n", ulx, uly, lrx, lry, cycle, st.list,
          (unsigned long)argb, (unsigned long)R.oml, (unsigned long)R.cimg);
    screen_quad(ulx, uly, lrx, lry, 0, 0, 0, 0, 0, argb, oargb);
    DIRTY();
}

static OUTLINE void do_texrect(uint32_t w0, uint32_t w1) {
    uint16_t xl, yl, xh, yh;
    uint8_t tile;
    int16_t s, t, dsdx, dtdy;
    gpvr_state_t st;
    uint32_t argb, oargb;
    int cycle = (R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3;
    float x0, y0, x1, y1, fs, ft, fdx, fdy;
    f3d_texrect_decode(w0, w1, &xl, &yl, &xh, &yh, &tile);   /* xh,yh = lower right (w0) */
    f3d_texrect_half1_decode(R.half1, &s, &t);
    f3d_texrect_half2_decode(R.half2, &dsdx, &dtdy);
    x0 = xl * 0.25f; y0 = yl * 0.25f; x1 = xh * 0.25f; y1 = yh * 0.25f;
    if (cycle >= 2) { x1 += 1.0f; y1 += 1.0f; }
    fs = s / 32.0f; ft = t / 32.0f;
    fdx = dsdx / 1024.0f; fdy = dtdy / 1024.0f;
    if (cycle == 2) fdx *= 0.25f;            /* copy mode: dsdx is x4 */
    {
        int saved_on = R.tex_on, saved_tile = R.tex_tile;
        R.tex_on = 1; R.tex_tile = tile;
        draw_state(1, 1, &st);
        R.tex_on = saved_on; R.tex_tile = saved_tile;
    }
    st.fog = 0;
    if (st.tex && (cycle == 2 || (x1 - x0) * fdx < 64.0f)) st.filter = PVR_FILTER_NEAREST; /* 1:1 blits (gfx.md) */
    layer_fix(&st);
    gpvr_set_state(&st);
    rect_colors(st.tex != NULL, &argb, &oargb);
    TRACE("  texrect %.0f,%.0f-%.0f,%.0f tile %d tex %d list %d argb %08lx cyc %d\n", x0, y0, x1, y1, tile,
          st.tex != NULL, st.list, (unsigned long)argb, cycle);
#if DB_GFX_TRACE
    if (sTrace) {
        tile_t *tt = &R.tile[tile];
        tmemload_t *ld = &R.loads[(tt->tmem >> 3) & 63];
        TRACE("    tile fmt %d siz %d line %d tmem %d s %d-%d t %d-%d mask %d/%d | load v%d addr %08lx bytes %lu dxt %d | oml %08lx ac %d\n",
              tt->fmt, tt->siz, tt->line, tt->tmem, tt->uls >> 2, tt->lrs >> 2, tt->ult >> 2, tt->lrt >> 2, tt->masks, tt->maskt,
              ld->valid, (unsigned long)ld->addr, (unsigned long)ld->bytes, ld->dxt, (unsigned long)R.oml, (int)(R.oml & 3));
        if (ld->valid) {
            const uint8_t *b = ram(ld->addr);
            TRACE("    data %02x%02x%02x%02x %02x%02x%02x%02x\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
        }
    }
#endif
    screen_quad(x0, y0, x1, y1, fs, ft, fs + (x1 - x0) * fdx, ft + (y1 - y0) * fdy, st.tex != NULL, argb, oargb);
    DIRTY();
}

/* ------------------------------------------------------------ interpreter */

static uint32_t sUnknown[256];

static HOT void run_dl(const uint32_t *pc) {
    const uint32_t *stack[DL_DEPTH];
    int sp = 0;
    for (;;) {
        uint32_t w0 = pc[0], w1 = pc[1];
        pc += 2;
        switch (w0 >> 24) {
        case F3D_G_TEXTURE: case F3D_G_SETOTHERMODE_H: case F3D_G_SETOTHERMODE_L: case F3D_G_SETGEOMETRYMODE:
        case F3D_G_CLEARGEOMETRYMODE: case F3D_G_SETTILE: case F3D_G_SETTILESIZE: case F3D_G_LOADBLOCK:
        case F3D_G_SETCOMBINE: case F3D_G_SETTIMG:
            DIRTY();
            break;
        }
        switch (w0 >> 24) {
        case F3D_G_MTX: do_mtx(w0, w1); break;
        case F3D_G_POPMTX: if (R.mv_top > 0) { R.mv_top--; R.mvp_dirty = R.lights_dirty = 1; } break;
        case F3D_G_VTX: { uint64_t t = TNOW(); do_vtx(w0, w1); sT_vtx += TNOW() - t; } break;
        case F3D_G_TRI1: { uint8_t a, b, c, f; f3d_tri1_decode(w1, &a, &b, &c, &f); if (a < 16 && b < 16 && c < 16) do_tri(a, b, c); } break;
        case F3D_G_DL: {
            uint8_t push; uint32_t addr;
            f3d_dl_decode(w0, w1, &push, &addr);
            if (R.cimg == R.zimg && R.cimg != 0) { if (!sShadowRec) { sShadowDL = addr; sShadowRec = 1; } }  /* pass 1 */
            else if (sShadowDL && addr == sShadowDL) { sShadowDL = 0; break; } /* pass 2: the volume replaces it */
            if (push) { if (sp == DL_DEPTH) return; stack[sp++] = pc; }
            pc = ram(addr);
        } break;
        case F3D_G_ENDDL: if (sp == 0) return; pc = stack[--sp]; break;
        case F3D_G_MOVEMEM: {
            uint8_t idx; uint32_t len, addr; f3d_movemem_decode(w0, w1, &idx, &len, &addr);
            const uint8_t *m = ram(addr);
            if (idx == F3D_G_MV_VIEWPORT) {
                const int16_t *vp = (const int16_t *)m;
                R.vp_sx = vp[0] * 0.25f; R.vp_sy = vp[1] * 0.25f; R.vp_tx = vp[4] * 0.25f; R.vp_ty = vp[5] * 0.25f;
                TRACE("  viewport scale %d %d trans %d %d\n", vp[0], vp[1], vp[4], vp[5]);
            } else if (idx >= F3D_G_MV_L0 && idx <= F3D_G_MV_L7) {
                int l = (idx - F3D_G_MV_L0) / 2;
                R.light_col[l][0] = m[0]; R.light_col[l][1] = m[1]; R.light_col[l][2] = m[2];
                R.light_dir[l][0] = (int8_t)m[8]; R.light_dir[l][1] = (int8_t)m[9]; R.light_dir[l][2] = (int8_t)m[10];
                R.lights_dirty = 1;
            } else if (idx == F3D_G_MV_LOOKATX || idx == F3D_G_MV_LOOKATY) {
                int l = idx == F3D_G_MV_LOOKATX ? 0 : 1;
                R.lookat[l][0] = (int8_t)m[8]; R.lookat[l][1] = (int8_t)m[9]; R.lookat[l][2] = (int8_t)m[10];
                R.lights_dirty = 1;
            }
        } break;
        case F3D_G_MOVEWORD: {
            uint8_t idx; uint16_t off; uint32_t data; f3d_moveword_decode(w0, w1, &idx, &off, &data);
            if (idx == F3D_G_MW_SEGMENT) R.seg[(off >> 2) & 15] = data & 0x1FFFFFFFu;
            else if (idx == F3D_G_MW_NUMLIGHT) { R.num_lights = f3d_numlights_from_data(data); R.lights_dirty = 1; }
            else if (idx == F3D_G_MW_FOG) f3d_fog_from_data(data, &R.fog_m, &R.fog_o);
            else if (idx == F3D_G_MW_LIGHTCOL) {
                uint8_t l = f3d_lightcol_index(off);
                if (l < 9) { R.light_col[l][0] = data >> 24; R.light_col[l][1] = data >> 16; R.light_col[l][2] = data >> 8; }
            }
        } break;
        case F3D_G_TEXTURE: {
            uint8_t lvl, tile, on; uint16_t s, t; f3d_texture_decode(w0, w1, &lvl, &tile, &on, &s, &t);
            R.tex_on = on; R.tex_tile = tile; R.tex_ss = s; R.tex_st = t;
        } break;
        case F3D_G_SETOTHERMODE_H: R.omh = f3d_othermode_apply(R.omh, w0, w1); break;
        case F3D_G_SETOTHERMODE_L: R.oml = f3d_othermode_apply(R.oml, w0, w1); break;
        case F3D_G_SETGEOMETRYMODE: R.geom |= w1; break;
        case F3D_G_CLEARGEOMETRYMODE: R.geom &= ~w1; break;
        case F3D_G_RDPHALF_1: R.half1 = w1; break;
        case F3D_G_RDPHALF_2: R.half2 = w1; break;
        case F3D_G_RDPHALF_CONT: break;
        case F3D_G_TEXRECT:
            /* E4 is followed by RDPHALF_1 (s,t) and RDPHALF_2 (dsdx,dtdy) */
            /* PW64's older F3D (geometry.c, sprite.c) emits E4, B3 (s,t), B2 (dsdx,dtdy); current gbi.h
               says E4, B4, B3. Take the two halves that follow, whichever numbering. */
            if ((pc[0] >> 24) >= 0xB2 && (pc[0] >> 24) <= 0xB4 && (pc[2] >> 24) >= 0xB2 && (pc[2] >> 24) <= 0xB4) {
                R.half1 = pc[1]; R.half2 = pc[3]; pc += 4;
            }
            do_texrect(w0, w1);
            break;
        case F3D_G_FILLRECT: do_fillrect(w0, w1); break;
        case F3D_G_SETTIMG: f3d_setimg_decode(w0, w1, &R.timg_fmt, &R.timg_siz, &R.timg_w, &R.timg_addr); break;
        case F3D_G_SETTILE: {
            struct f3d_settile s; f3d_settile_decode(w0, w1, &s);
            tile_t *t = &R.tile[s.tile];
            t->fmt = s.fmt; t->siz = s.siz; t->line = s.line; t->tmem = s.tmem;
            t->cms = s.cms; t->cmt = s.cmt; t->masks = s.masks; t->maskt = s.maskt; t->shifts = s.shifts; t->shiftt = s.shiftt;
        } break;
        case F3D_G_SETTILESIZE: {
            uint8_t tile; uint16_t uls, ult, lrs, lrt; f3d_tilesize_decode(w0, w1, &tile, &uls, &ult, &lrs, &lrt);
            R.tile[tile].uls = uls; R.tile[tile].ult = ult; R.tile[tile].lrs = lrs; R.tile[tile].lrt = lrt;
        } break;
        case F3D_G_LOADBLOCK: {
            uint8_t tile; uint16_t uls, ult, lrs, dxt; f3d_loadblock_decode(w0, w1, &tile, &uls, &ult, &lrs, &dxt);
            tmemload_t *ld = &R.loads[(R.tile[tile].tmem >> 3) & 63];
            unsigned bpt = R.timg_siz == 3 ? 4 : R.timg_siz == 2 ? 2 : 1;   /* LOADBLOCK in SETTIMG's size */
            ld->addr = R.timg_addr; ld->bytes = (lrs + 1) * bpt; ld->dxt = dxt; ld->valid = 1;
            if (R.timg_siz == 0) ld->bytes = (lrs + 2) / 2;
        } break;
        case F3D_G_SETCOMBINE: R.cc0 = w0; R.cc1 = w1; break;
        case F3D_G_SETPRIMCOLOR: if (R.prim_raw != w1) DIRTY(); R.prim_raw = w1;
                                 R.prim[0] = ((w1 >> 24) & 0xFF) / 255.0f; R.prim[1] = ((w1 >> 16) & 0xFF) / 255.0f;
                                 R.prim[2] = ((w1 >> 8) & 0xFF) / 255.0f; R.prim[3] = (w1 & 0xFF) / 255.0f; break;
        case F3D_G_SETENVCOLOR: if (R.env_raw != w1) DIRTY(); R.env_raw = w1;
                                R.env[0] = ((w1 >> 24) & 0xFF) / 255.0f; R.env[1] = ((w1 >> 16) & 0xFF) / 255.0f;
                                R.env[2] = ((w1 >> 8) & 0xFF) / 255.0f; R.env[3] = (w1 & 0xFF) / 255.0f; break;
        case F3D_G_SETFOGCOLOR: gpvr_fog_color(0xFF000000u | (w1 >> 8)); break;
        case F3D_G_SETBLENDCOLOR: break;
        case F3D_G_SETFILLCOLOR: R.fill = w1; break;
        case F3D_G_SETCIMG:
            if (R.cimg == R.zimg && R.cimg != 0 && (w1 & 0x1FFFFFFFu) != R.zimg) shadow_flush();
            R.cimg = w1 & 0x1FFFFFFFu;
            break;
        case F3D_G_SETZIMG: R.zimg = w1 & 0x1FFFFFFFu; break;
        case F3D_G_RDPLOADSYNC: case F3D_G_RDPPIPESYNC: case F3D_G_RDPTILESYNC: case F3D_G_RDPFULLSYNC:
        case F3D_G_SETSCISSOR: case F3D_G_SPNOOP: case F3D_G_NOOP:
            break;
        default:
            sUnknown[w0 >> 24]++;
            break;
        }
    }
}

static int sInit;
static uint32_t sFrames;


/* The thread inside gfx_f3d_run while a display list is interpreted (else NULL): prof.c's in-DL sampler. */
void *volatile wings64_gfx_dl_thread;

HOT void gfx_f3d_run(const void *dl) {
    if (!sInit) {
        sInit = 1;
        R.vp_sx = 160.0f; R.vp_sy = 120.0f; R.vp_tx = 160.0f; R.vp_ty = 120.0f;
        for (int i = 0; i < 16; i++) R.mv[0].elem[i] = R.proj.elem[i] = (i % 5) == 0 ? 1.0f : 0.0f;
    }
    uint64_t t0 = timer_us_gettime64(), t1, t2, t3;
    static uint64_t sDlCpuSum, sDlWallSum;
    uint64_t c1;
#ifdef DB_GFX_BG_MAGENTA
    gpvr_frame_begin(0xFFFF00FFu);   /* debug: uncovered pixels show magenta */
#else
    gpvr_frame_begin(0xFF000000u);
#endif
    t1 = timer_us_gettime64();
    c1 = thd_get_cpu_time(thd_get_current());
    wings64_gfx_dl_thread = thd_get_current();
    DIRTY();
    R.mv_top = 0;
    R.mvp_dirty = 1;
    sLayerZ = 0.0f; sLayerN = sSceneZ = 0;
#if WINGS64_TEX_MEMO
    sTexFrame++;            /* per-frame memo: textures may change between display lists */
#endif
#if DB_GFX_TRACE
    {
        /* Trace only in flight (game state 5) to keep menus at speed; DB_GFX_TRACE_ALL traces everything. */
        extern int wings64_game_state(void);
#ifdef DB_GFX_TRACE_ALL
        sTrace = (sFrames % 15) == 0;
#else
        sTrace = (sFrames % 30) == 0 && wings64_game_state() == 5;
#endif
    }
    sListTris[0] = sListTris[1] = sListTris[2] = 0;
    sRejTrivial = sRejCull = sClipped = sVtxLoads = 0;
    sMinW = 1e30f; sMaxW = -1e30f;
    TRACE("trace frame %lu\n", (unsigned long)sFrames);
#endif
    run_dl(dl);
    shadow_flush();
    sShadowDL = 0;
#if DB_GFX_TRACE
    TRACE("  tris op %lu pt %lu tr %lu | rej %lu cull %lu clip %lu vtx %lu w %.3g..%.3g\n", (unsigned long)sListTris[0],
          (unsigned long)sListTris[1], (unsigned long)sListTris[2], (unsigned long)sRejTrivial, (unsigned long)sRejCull,
          (unsigned long)sClipped, (unsigned long)sVtxLoads, sMinW, sMaxW);
#endif
    t2 = timer_us_gettime64();
    wings64_gfx_dl_thread = NULL;
    sDlCpuSum += (thd_get_cpu_time(thd_get_current()) - c1) / 1000;   /* ns -> us */
    sDlWallSum += t2 - t1;
    gpvr_frame_end();
    t3 = timer_us_gettime64();
    if (++sFrames % 30 == 0) {
        db_log("gfx: wait %lu us, dl %lu us, finish %lu us, resets %lu, dl avg30 wall %lu cpu %lu us\n",
               (unsigned long)(t1 - t0), (unsigned long)(t2 - t1), (unsigned long)(t3 - t2),
               (unsigned long)gpvr_stats.resets, (unsigned long)(sDlWallSum / 30), (unsigned long)(sDlCpuSum / 30));
        sDlWallSum = sDlCpuSum = 0;
        if (DB_GFX_PROF) db_log("gfx: 30f us: vtx %lu state %lu conv %lu emit %lu | fit %lu tex %lu set %lu\n", (unsigned long)sT_vtx, (unsigned long)sT_state,
               (unsigned long)sT_conv, (unsigned long)sT_emit, (unsigned long)sT_fit, (unsigned long)sT_tex, (unsigned long)sT_set);
        sT_fit = sT_tex = sT_set = 0;
#if WINGS64_CC_CACHE
        db_log("gfx: cc cache hit %lu miss %lu\n", (unsigned long)gfx_cc_cache_hits, (unsigned long)gfx_cc_cache_misses);
#endif
        sT_vtx = sT_state = sT_conv = sT_emit = sT_other = 0;
        db_log("gfx: frame %lu tris %lu states %lu tex up %lu hit %lu ovf %lu\n", (unsigned long)sFrames,
               (unsigned long)gpvr_stats.tris, (unsigned long)gpvr_stats.state_changes,
               (unsigned long)gpvr_stats.tex_uploads, (unsigned long)gpvr_stats.tex_hits, (unsigned long)gpvr_stats.overflow);
        for (int i = 0; i < 256; i++)
            if (sUnknown[i]) { db_log("gfx: unhandled op %02X x%lu\n", i, (unsigned long)sUnknown[i]); sUnknown[i] = 0; }
    }
}
