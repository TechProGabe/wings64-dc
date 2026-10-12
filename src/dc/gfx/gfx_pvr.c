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
#define VB_MOD (64 * 1024)

gpvr_stats_t gpvr_stats;
static uint8_t *vbuf[3];
static const pvr_list_t list_id[3] = { PVR_LIST_OP_POLY, PVR_LIST_PT_POLY, PVR_LIST_TR_POLY };
static gpvr_state_t cur[3];
static int cur_valid[3];
int gpvr_active = 0;             /* list of the current state (read by gpvr_tri_pv_inline) */
#define active gpvr_active
uint32_t gpvr_state_serial;      /* bumped whenever the active list or its header may have changed */
static size_t used[3], cap[3];

#if WINGS64_SHADOW_MOD
/* OP modifier list (shadow volumes): its own DMA buffer, written directly (64-byte volume triangles). */
static uint8_t *mbuf, *mbase, *mwp, *mwend;
static float shadow_scale = 1.0f, shadow_scale_set = -1.0f;
#endif

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
#if WINGS64_SHADOW_MOD
    mbuf = memalign(32, VB_MOD);
    if (!mbuf) arch_abort();
    pvr_set_vertbuf(PVR_LIST_OP_MOD, mbuf, VB_MOD);
#endif
}

void gpvr_frame_begin(uint32_t bg_argb) {
    if (pvr_wait_ready() < 0) {
        /* KOS gives up after 100 ms: the previous scene never finished (a wedged TA/ISP on hardware). */
        static unsigned timeouts;
        if (++timeouts <= 8 || timeouts % 100 == 0) db_log("pvr: wait ready timed out (%u so far)\n", timeouts);
    }
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
#if WINGS64_SHADOW_MOD
    mbase = mwp = pvr_vertbuf_tail(PVR_LIST_OP_MOD);
    mwend = mbase + VB_MOD / 2 - 64;
#endif
}

#if WINGS64_SHADOW_MOD
static void mod_hdr(uint32_t mode) {
    pvr_mod_compile((pvr_mod_hdr_t *)mwp, PVR_LIST_OP_MOD, mode, PVR_CULLING_NONE);
    /* The closing header also needs the parameter control word's Volume bit (bit 6: last polygon of the
       volume), which KOS v2.2.2's pvr_mod_compile never sets. Without it Flycast ORs the closing triangle
       in as an open volume (it shadowed that triangle's whole projection). */
    if (mode != PVR_MODIFIER_OTHER_POLY) ((pvr_mod_hdr_t *)mwp)->cmd |= 1u << 6;
    mwp += 32;
}

static void mod_tri(const float *t) {
    pvr_modifier_vol_t *v = (pvr_modifier_vol_t *)mwp;
    v->flags = PVR_CMD_VERTEX_EOL;
    v->ax = t[0]; v->ay = t[1]; v->az = t[2];
    v->bx = t[3]; v->by = t[4]; v->bz = t[5];
    v->cx = t[6]; v->cy = t[7]; v->cz = t[8];
    v->d1 = v->d2 = v->d3 = v->d4 = v->d5 = v->d6 = 0;
    mwp += 64;
}

/* One volume = one OTHER_POLY header and its triangles, then an INCLUDE_LAST_POLY header and the closing
   triangle; inside = odd number of volume surfaces in front of the pixel. A header per triangle restarts
   the volume (Flycast showed only the last triangle shadowing). */
void gpvr_mod_volume(const float *tris, int n) {
    if (n <= 0) return;
    if (mwp + 64 + (size_t)n * 64 > mwend) { gpvr_stats.overflow++; return; }
    if (n > 1) {
        mod_hdr(PVR_MODIFIER_OTHER_POLY);
        for (int i = 0; i < n - 1; i++) mod_tri(tris + i * 9);
    }
    mod_hdr(PVR_MODIFIER_INCLUDE_LAST_POLY);
    mod_tri(tris + (n - 1) * 9);
}

void gpvr_shadow_scale(float scale) { shadow_scale = scale; }
#endif

void gpvr_frame_end(void) {
#if WINGS64_DIRECT_EMIT
    for (int i = 0; i < 3; i++)
        if (wp[i] != wbase[i]) pvr_vertbuf_written(list_id[i], (size_t)(wp[i] - wbase[i]));
#endif
#if WINGS64_SHADOW_MOD
    if (mwp == mbase) {
        /* An enabled list must hold something; KOS's blank filler is a polygon header, not a volume. */
        static const float empty[9] = { -64.0f, -64.0f, 1e-6f, -63.0f, -64.0f, 1e-6f, -64.0f, -63.0f, 1e-6f };
        gpvr_mod_volume(empty, 1);
    }
    pvr_vertbuf_written(PVR_LIST_OP_MOD, (size_t)(mwp - mbase));
    if (shadow_scale != shadow_scale_set) { pvr_set_shadow_scale(1, shadow_scale); shadow_scale_set = shadow_scale; }
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
#if WINGS64_SHADOW_MOD
    /* Depth-tested geometry receives shadow volumes (cheap shadow: colour scaled inside a volume; OP volumes
       act on the OP and PT lists). Screen-space layers (HUD, sky) stay as they are. */
    if (st->depth_cmp != PVR_DEPTHCMP_ALWAYS) {
        cxt.fmt.modifier = PVR_MODIFIER_ENABLE;
        cxt.gen.modifier_mode = PVR_MODIFIER_CHEAP_SHADOW;
    }
#endif
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
