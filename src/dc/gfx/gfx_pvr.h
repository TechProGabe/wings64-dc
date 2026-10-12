/* gfx_pvr.h — PowerVR backend for the Fast3D interpreter (PLAN D4 phase A).
 * Vertex DMA mode: each list (OP/PT/TR) fills its own RAM buffer in any order during the frame;
 * pvr_scene_finish DMAs them. Poly headers are compiled once per distinct draw state.
 */
#ifndef WINGS64_GFX_PVR_H
#define WINGS64_GFX_PVR_H

#include <stdint.h>
#include <dc/pvr.h>

enum { GPVR_OP = 0, GPVR_PT = 1, GPVR_TR = 2 };

typedef struct {
    uint8_t  list;          /* GPVR_OP / GPVR_PT / GPVR_TR */
    uint8_t  depth_cmp;     /* PVR_DEPTHCMP_* */
    uint8_t  depth_write;   /* 1 = write */
    uint8_t  blend_src, blend_dst;   /* PVR_BLEND_* (TR only; OP/PT use ONE/ZERO) */
    uint8_t  fog;           /* 1 = vertex fog (alpha of oargb) */
    uint8_t  filter;        /* PVR_FILTER_* */
    uint8_t  clamp;         /* PVR_UVCLAMP_* */
    uint8_t  flip;          /* PVR_UVFLIP_* (N64 mirror) */
    uint8_t  env;           /* PVR_TXRENV_* */
    uint8_t  txr_alpha;     /* 1 = use texture alpha */
    uint8_t  pad;
    pvr_ptr_t tex;          /* NULL = untextured */
    uint32_t tex_fmt;       /* PVR_TXRFMT_* | twiddled */
    uint16_t tex_w, tex_h;
} gpvr_state_t;

typedef struct {
    float x, y, z;          /* screen pixels (640x480), z = 1/w */
    float u, v;
    uint32_t argb, oargb;   /* oargb alpha = fog factor when fog is on */
} gpvr_vtx_t;

void gpvr_init(void);
void gpvr_frame_begin(uint32_t bg_argb);
void gpvr_frame_end(void);
void gpvr_set_state(const gpvr_state_t *st);
/* Changes whenever the PVR side's active list/header may differ from the last gpvr_set_state: a caller
   that set the same state and sees the same serial may skip the call. */
extern uint32_t gpvr_state_serial;
/* One triangle in the current state's list. Vertices in any winding (culling is done before). */
void gpvr_tri(const gpvr_vtx_t *a, const gpvr_vtx_t *b, const gpvr_vtx_t *c);
void gpvr_fog_color(uint32_t argb);

/* Shadow volumes as PVR cheap-shadow modifier volumes (gfx_f3d.c shadow_*). Kill switch
   -DWINGS64_SHADOW_MOD=0: no modifier list, shadow volumes are not drawn at all. */
#ifndef WINGS64_SHADOW_MOD
#define WINGS64_SHADOW_MOD 1
#endif
/* One closed volume: n triangles, 9 floats each (x, y, z per corner; screen pixels, z = 1/w). */
void gpvr_mod_volume(const float *tris, int n);
extern int gpvr_mod_off;   /* debug toggle: headers never get the modifier bit */
/* Cheap-shadow intensity inside volumes (1 = no change, 0 = black); applied at frame end. */
void gpvr_shadow_scale(float scale);
/* Same, from ready PVR vertices (flags are set here). */
void gpvr_tri_pv(const pvr_vertex_t *a, const pvr_vertex_t *b, const pvr_vertex_t *c);

/* Frozen frame (patches/026): copy the last finished frame's lists (returns 0 if out of memory or not
   supported), write that copy at the start of the current frame, free it. Direct-emit builds only. */
int gpvr_freeze_capture(void);
void gpvr_freeze_replay(void);
void gpvr_freeze_release(void);

/* Persistent frame buffer (patches/028): a PERSIST_W x PERSIST_H RGB565 non-twiddled texture holding the
   last finished frame (640x480 in its top-left). begin allocates it (NULL if VRAM is short), capture copies
   the front buffer into it (call between scenes), end frees it. */
#define PERSIST_W 1024
#define PERSIST_H 512
pvr_ptr_t gpvr_persist_begin(void);
typedef struct { int16_t x0, y0, x1, y1, tex; } gpvr_rect_t;
void gpvr_persist_capture(const gpvr_rect_t *r, int n);   /* n <= 0: whole screen */
int gpvr_list_rects(int list, size_t skip, gpvr_rect_t *r, int n, int max, gpvr_rect_t *u);
void gpvr_persist_forget(void);   /* textured strips may have been drawn over: copy them again */
void gpvr_persist_end(void);
size_t gpvr_list_bytes(int list);   /* bytes written to a list this frame */
uint32_t gpvr_list_hash(int list);  /* hash of a list's headers and vertex positions this frame */

typedef struct { uint32_t tris, state_changes, tex_uploads, tex_hits, overflow, resets, opb_oom, ta_ovf; } gpvr_stats_t;
extern gpvr_stats_t gpvr_stats;

/* docs/fps-plan.md change 1: direct emit into this frame's half of the list's DMA vertex buffer, one
   allocated cache line per 32-byte record. Kill switch -DWINGS64_DIRECT_EMIT=0. The inline triangle writer
   lets the interpreter keep its whole per-triangle path in one hot section (change 2). */
#ifndef WINGS64_DIRECT_EMIT
#define WINGS64_DIRECT_EMIT 1
#endif
#if WINGS64_DIRECT_EMIT
#include <sh4zam/shz_mem.h>
extern uint8_t *gpvr_wp[3], *gpvr_wend[3];
extern int gpvr_active;
static inline void gpvr_put32(uint8_t *dst, const void *src) {
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *d = (uint32_t *)dst;
    shz_dcache_alloc_line(d);
    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
    d[4] = s[4]; d[5] = s[5]; d[6] = s[6]; d[7] = s[7];
}
static inline void gpvr_tri_pv_inline(const pvr_vertex_t *a, const pvr_vertex_t *b, const pvr_vertex_t *c) {
    uint8_t *d = gpvr_wp[gpvr_active];
    if (d + 96 > gpvr_wend[gpvr_active]) { gpvr_stats.overflow++; return; }
    gpvr_put32(d, a);
    gpvr_put32(d + 32, b);
    gpvr_put32(d + 64, c);
    ((pvr_vertex_t *)d)->flags = PVR_CMD_VERTEX;
    ((pvr_vertex_t *)(d + 32))->flags = PVR_CMD_VERTEX;
    ((pvr_vertex_t *)(d + 64))->flags = PVR_CMD_VERTEX_EOL;
    gpvr_wp[gpvr_active] = d + 96;
    gpvr_stats.tris++;
}
#else
#define gpvr_tri_pv_inline gpvr_tri_pv
#endif

/* Texture cache: key = (address, format word, size, content sample). Returns VRAM texture or NULL. */
typedef struct {
    uint32_t addr, fmtkey, sample;
    uint16_t w, h;          /* pow2 PVR dims */
    uint32_t pvr_fmt;
    pvr_ptr_t ptr;
} gpvr_tex_t;
gpvr_tex_t *gpvr_tex_lookup(uint32_t addr, uint32_t fmtkey, uint32_t sample);
/* Upload `pixels` (w x h, linear 16-bit) twiddled; returns the cache entry or NULL if VRAM is full. */
gpvr_tex_t *gpvr_tex_insert(uint32_t addr, uint32_t fmtkey, uint32_t sample, const uint16_t *pixels,
                            unsigned w, unsigned h, uint32_t pvr_fmt);
void gpvr_tex_reset(void);   /* level load / VRAM pressure: drop every texture (after a fence) */


#endif
