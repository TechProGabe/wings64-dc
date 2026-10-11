/* gfx_pvr.c — PowerVR backend: DMA vertex buffers, header cache, texture cache. */
#include "gfx_pvr.h"
#include "gfx_texconv.h"
#include "../dcpvr.h"
#include "../db.h"

#include <kos.h>
#include <malloc.h>
#include <string.h>
#include <sh4zam/shz_sh4zam.h>

#define VB_OP (768 * 1024)
#define VB_PT (256 * 1024)
#define VB_TR (512 * 1024)

gpvr_stats_t gpvr_stats;
static uint8_t *vbuf[3];
static const pvr_list_t list_id[3] = { PVR_LIST_OP_POLY, PVR_LIST_PT_POLY, PVR_LIST_TR_POLY };
static gpvr_state_t cur[3];
static int cur_valid[3];
int gpvr_active = 0;             /* list of the current state (read by gpvr_tri_pv_inline) */
#define active gpvr_active
uint32_t gpvr_state_serial;      /* bumped whenever the active list or its header may have changed */
static size_t used[3], cap[3];

/* Direct emit (docs/fps-plan.md change 1): vertices and headers are written straight into this frame's
   half of each list's DMA buffer (pvr_set_vertbuf splits the buffer in two), one allocated cache line per
   32-byte record, instead of a stack copy plus pvr_list_prim's memcpy (which also read-allocated every
   output line). The written amount is handed to KOS once per list at frame end.
   Kill switch: -DWINGS64_DIRECT_EMIT=0 (pvr_list_prim path). */
#if WINGS64_DIRECT_EMIT
static uint8_t *wbase[3];
uint8_t *gpvr_wp[3], *gpvr_wend[3];       /* write pointer / limit per list (gfx_pvr.h inline emitter) */
#define wp gpvr_wp
#define wend gpvr_wend
#endif

void gpvr_init(void) {
    cap[0] = VB_OP; cap[1] = VB_PT; cap[2] = VB_TR;
    for (int i = 0; i < 3; i++) {
        vbuf[i] = memalign(32, cap[i]);
        if (!vbuf[i]) arch_abort();
        pvr_set_vertbuf(list_id[i], vbuf[i], cap[i]);
    }
    pvr_set_zclip(0.0f);
}

void gpvr_frame_begin(uint32_t bg_argb) {
    pvr_wait_ready();
    pvr_set_bg_color(((bg_argb >> 16) & 0xFF) / 255.0f, ((bg_argb >> 8) & 0xFF) / 255.0f, (bg_argb & 0xFF) / 255.0f);
    pvr_scene_begin();
    memset(cur_valid, 0, sizeof cur_valid);
    gpvr_state_serial++;
    memset(used, 0, sizeof used);
#if WINGS64_DIRECT_EMIT
    for (int i = 0; i < 3; i++) {
        wbase[i] = wp[i] = pvr_vertbuf_tail(list_id[i]);
        wend[i] = wbase[i] + cap[i] / 2 - 64;   /* this frame's half, as pvr_set_vertbuf sized it */
    }
#endif
}

void gpvr_frame_end(void) {
#if WINGS64_DIRECT_EMIT
    for (int i = 0; i < 3; i++)
        if (wp[i] != wbase[i]) pvr_vertbuf_written(list_id[i], (size_t)(wp[i] - wbase[i]));
#endif
    pvr_scene_finish();
}

/* Vertex fog (PVR_FOG_VERTEX) blends toward the VERTEX fog colour register, not the table one. */
void gpvr_fog_color(uint32_t argb) {
    static uint32_t last = 0xFFFFFFFFu;
    if (argb == last) return;
    last = argb;
    /* KOS v2.2.2's pvr_fog_vertex_color() is an assert("not implemented") stub (kb/traps.md): write the
       vertex fog colour register directly, and the table colour too. */
    PVR_SET(PVR_FOG_VERTEX_COLOR, 0xFF000000u | (argb & 0xFFFFFFu));
    pvr_fog_table_color(1.0f, ((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f);
}

#if WINGS64_DIRECT_EMIT
#define put32 gpvr_put32
static void emit(int list, const void *data, size_t n) {
    const uint8_t *src = data;
    if (wp[list] + n > wend[list]) { gpvr_stats.overflow++; return; }
    for (size_t o = 0; o < n; o += 32) put32(wp[list] + o, src + o);
    wp[list] += n;
}
#else
static void emit(int list, const void *data, size_t n) {
    if (used[list] + n > cap[list] / 2 - 64) { gpvr_stats.overflow++; return; }   /* this frame's half */
    pvr_list_prim(list_id[list], data, n);
    used[list] += n;
}
#endif

static void compile_and_emit(const gpvr_state_t *st) {
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    if (st->tex) {
        pvr_poly_cxt_txr(&cxt, list_id[st->list], st->tex_fmt, st->tex_w, st->tex_h, st->tex, st->filter);
        cxt.txr.uv_clamp = st->clamp;
        cxt.txr.uv_flip = st->flip;
        cxt.txr.env = st->env;
        cxt.txr.alpha = st->txr_alpha ? PVR_TXRALPHA_ENABLE : PVR_TXRALPHA_DISABLE;
    } else {
        pvr_poly_cxt_col(&cxt, list_id[st->list]);
    }
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.gen.specular = PVR_SPECULAR_ENABLE;          /* offset color carries fog / additive terms */
    cxt.gen.fog_type = st->fog ? PVR_FOG_VERTEX : PVR_FOG_DISABLE;
    cxt.depth.comparison = st->depth_cmp;
    cxt.depth.write = st->depth_write ? PVR_DEPTHWRITE_ENABLE : PVR_DEPTHWRITE_DISABLE;
    if (st->list == GPVR_TR) {
        cxt.blend.src = st->blend_src;
        cxt.blend.dst = st->blend_dst;
        cxt.gen.alpha = PVR_ALPHA_ENABLE;
    }
    pvr_poly_compile(&hdr, &cxt);
    emit(st->list, &hdr, sizeof hdr);
}

void gpvr_set_state(const gpvr_state_t *st) {
    if (active != st->list) gpvr_state_serial++;
    active = st->list;
    if (cur_valid[active] && memcmp(&cur[active], st, sizeof *st) == 0) return;
    gpvr_state_serial++;
    cur[active] = *st;
    cur_valid[active] = 1;
    gpvr_stats.state_changes++;
    compile_and_emit(st);
}

void gpvr_tri(const gpvr_vtx_t *a, const gpvr_vtx_t *b, const gpvr_vtx_t *c) {
    pvr_vertex_t v[3] __attribute__((aligned(32)));
    const gpvr_vtx_t *s[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        v[i].flags = i == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        v[i].x = s[i]->x; v[i].y = s[i]->y; v[i].z = s[i]->z;
        v[i].u = s[i]->u; v[i].v = s[i]->v;
        v[i].argb = s[i]->argb; v[i].oargb = s[i]->oargb;
    }
    emit(active, v, sizeof v);
    gpvr_stats.tris++;
}

void gpvr_tri_pv(const pvr_vertex_t *a, const pvr_vertex_t *b, const pvr_vertex_t *c) {
#if WINGS64_DIRECT_EMIT
    uint8_t *d = wp[active];
    if (d + 96 > wend[active]) { gpvr_stats.overflow++; return; }
    put32(d, a);
    put32(d + 32, b);
    put32(d + 64, c);
    ((pvr_vertex_t *)d)->flags = PVR_CMD_VERTEX;          /* callers' flags are not trusted */
    ((pvr_vertex_t *)(d + 32))->flags = PVR_CMD_VERTEX;
    ((pvr_vertex_t *)(d + 64))->flags = PVR_CMD_VERTEX_EOL;
    wp[active] = d + 96;
#else
    pvr_vertex_t v[3] __attribute__((aligned(32)));
    v[0] = *a; v[1] = *b; v[2] = *c;
    v[0].flags = v[1].flags = PVR_CMD_VERTEX;
    v[2].flags = PVR_CMD_VERTEX_EOL;
    emit(active, v, sizeof v);
#endif
    gpvr_stats.tris++;
}

/* ------------------------------------------------------------ texture cache */

#define TEX_SLOTS 1024
#define TEX_BUCKETS 512
static gpvr_tex_t texs[TEX_SLOTS];
static int16_t tex_next[TEX_SLOTS], tex_head[TEX_BUCKETS];
static int ntex;

static unsigned tex_bucket(uint32_t addr, uint32_t fmtkey) { return ((addr >> 3) ^ (fmtkey * 2654435761u >> 9)) & (TEX_BUCKETS - 1); }

gpvr_tex_t *gpvr_tex_lookup(uint32_t addr, uint32_t fmtkey, uint32_t sample) {
    for (int i = ntex ? tex_head[tex_bucket(addr, fmtkey)] : -1; i >= 0; i = tex_next[i])
        if (texs[i].addr == addr && texs[i].fmtkey == fmtkey && texs[i].sample == sample) {
            gpvr_stats.tex_hits++;
            return &texs[i];
        }
    return NULL;
}

void gpvr_tex_reset(void) {
    gpvr_stats.resets++;
    dc_pvr_vram_fence();
    for (int i = 0; i < ntex; i++) pvr_mem_free(texs[i].ptr);
    ntex = 0;
    memset(tex_head, 0xFF, sizeof tex_head);
}

gpvr_tex_t *gpvr_tex_insert(uint32_t addr, uint32_t fmtkey, uint32_t sample, const uint16_t *pixels,
                            unsigned w, unsigned h, uint32_t pvr_fmt) {
    static uint16_t twid[1024 * 64] __attribute__((aligned(32)));
    size_t bytes = (size_t)w * h * 2;
    pvr_ptr_t p;
    gpvr_tex_t *t;
    if (bytes > sizeof twid) return NULL;
    if (ntex == TEX_SLOTS || pvr_mem_available() < bytes + 64 * 1024) {
        /* DIVERGES: VRAM or slot pressure drops the whole cache (mid-frame textures re-upload); PW64
           reuses heap addresses across levels, so a full reset is also the level-load policy. */
        gpvr_tex_reset();
    }
    p = pvr_mem_malloc(bytes);
    if (!p) return NULL;
    texconv_twiddle16(pixels, twid, w, h);
    pvr_txr_load(twid, p, bytes);
    if (ntex == 0) memset(tex_head, 0xFF, sizeof tex_head);
    t = &texs[ntex];
    tex_next[ntex] = tex_head[tex_bucket(addr, fmtkey)];
    tex_head[tex_bucket(addr, fmtkey)] = (int16_t)ntex;
    ntex++;
    t->addr = addr; t->fmtkey = fmtkey; t->sample = sample;
    t->w = (uint16_t)w; t->h = (uint16_t)h;
    t->pvr_fmt = pvr_fmt | PVR_TXRFMT_TWIDDLED;
    t->ptr = p;
    gpvr_stats.tex_uploads++;
    return t;
}
