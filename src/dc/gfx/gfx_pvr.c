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
int gpvr_mod_off;                /* debug toggle: no modifier bit on any header (gfx_f3d.c DB_GFX_TOGGLES) */
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

/* Tile Accelerator overflow (pop-in triage, hardware only: Flycast models neither): the TA raises
   OPB_OUTOFMEM when the object pointer buffers and their overflow pool run out (it then drops the rest of
   the list for that frame) and TA_INPUT_OVERFLOW when its vertex input queue overflows. Counted here and
   logged on the 30-frame gfx: line. */
static void ta_error(uint32_t code, void *data) {
    (void)data;
    if (code == ASIC_EVT_PVR_OPB_OUTOFMEM) gpvr_stats.opb_oom++;
    else gpvr_stats.ta_ovf++;
}

void gpvr_init(void) {
    cap[0] = VB_OP; cap[1] = VB_PT; cap[2] = VB_TR;
    for (int i = 0; i < 3; i++) {
        vbuf[i] = memalign(32, cap[i]);
        if (!vbuf[i]) arch_abort();
        pvr_set_vertbuf(list_id[i], vbuf[i], cap[i]);
    }
    pvr_set_zclip(0.0f);
    asic_evt_set_handler(ASIC_EVT_PVR_OPB_OUTOFMEM, ta_error, NULL);
    asic_evt_enable(ASIC_EVT_PVR_OPB_OUTOFMEM, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_TA_INPUT_OVERFLOW, ta_error, NULL);
    asic_evt_enable(ASIC_EVT_PVR_TA_INPUT_OVERFLOW, ASIC_IRQ_DEFAULT);
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

#if WINGS64_DIRECT_EMIT
/* Frozen frame (patches/026, gfx_f3d.c wings64_fb_freeze). Each list's records for a frame stay in that
   frame's half of the DMA buffer until the frame after next, so the last finished frame can be copied out
   between frames and written again, ahead of new polygons, into later frames. */
static uint8_t *last_base[4];
static size_t last_len[4];
static uint8_t *frz[4];
static size_t frz_len[4];

int gpvr_freeze_capture(void) {
    gpvr_freeze_release();
    for (int i = 0; i < 4; i++) {
        if (!last_len[i]) continue;
        frz[i] = memalign(32, last_len[i]);
        if (!frz[i]) { gpvr_freeze_release(); return 0; }
        shz_memcpy(frz[i], last_base[i], last_len[i]);
        frz_len[i] = last_len[i];
    }
    return 1;
}

void gpvr_freeze_replay(void) {
    for (int i = 0; i < 3; i++)
        if (frz_len[i] && wp[i] + frz_len[i] <= wend[i]) { shz_memcpy(wp[i], frz[i], frz_len[i]); wp[i] += frz_len[i]; }
#if WINGS64_SHADOW_MOD
    if (frz_len[3] && mwp + frz_len[3] <= mwend) { shz_memcpy(mwp, frz[3], frz_len[3]); mwp += frz_len[3]; }
#endif
    gpvr_state_serial++;   /* the replayed records end with their own last header */
    memset(cur_valid, 0, sizeof cur_valid);
}

void gpvr_freeze_release(void) {
    for (int i = 0; i < 4; i++) { free(frz[i]); frz[i] = NULL; frz_len[i] = 0; }
}
#else
int gpvr_freeze_capture(void) { return 0; }   /* the pvr_list_prim path keeps no frame copy */
void gpvr_freeze_replay(void) { }
void gpvr_freeze_release(void) { }
#endif

/* Persistent frame buffer (patches/028, gfx_f3d.c wings64_fb_persist). The N64 never clears its frame buffer
   by itself; the photo album draws each thumbnail once and later frames only redraw text. While persistence
   is on, the last finished frame is read back from VRAM (KOS front buffer, linear RGB565 640x480 in the
   32-bit window, as vid_set_vram maps it) into a 1024x512 non-twiddled RGB565 texture that gfx_f3d.c draws
   as the frame's first layer. ~600 KB of uncached VRAM reads per frame: album screens only. */
static pvr_ptr_t persist_tex;

/* On composite/S-video KOS sets the render scaler to 0x401: lines are smoothed (480i flicker filter) and the
   image shrinks by 1/1024 as tiles are written to the frame buffer. Re-capturing the whole screen fed that back
   every time (hardware run 7: the album blurred and crept smaller as the cursor moved; Flycast ignores the
   scaler). Writing SCALER_CFG at run time hung the PVR on hardware (runs 8 and 9), so the scaler stays KOS's:
   instead a capture copies only the rectangles the finished frame drew into, so each pixel passes the filter
   about once, as on an ordinary frame. */

void gpvr_persist_forget(void);

pvr_ptr_t gpvr_persist_begin(void) {
    if (!persist_tex) {
        if (pvr_mem_available() < PERSIST_W * PERSIST_H * 2 + 64 * 1024) gpvr_tex_reset();
        persist_tex = pvr_mem_malloc(PERSIST_W * PERSIST_H * 2);
        gpvr_persist_forget();
    }
    return persist_tex;
}

void gpvr_persist_end(void) {
    if (!persist_tex) return;
    dc_pvr_vram_fence();
    pvr_mem_free(persist_tex);
    persist_tex = NULL;
}

/* Copy the finished frame's rectangles r[0..n) (output pixels, x1/y1 exclusive) into the persistent texture;
   n <= 0 copies the whole screen. */
void gpvr_persist_capture(const gpvr_rect_t *r, int n) {
    static const gpvr_rect_t full = { 0, 0, 640, 480, 0 };
    uintptr_t front;
    const uint8_t *src;
    uint8_t *dst = (uint8_t *)persist_tex;
    if (!dst) return;
    if (n <= 0) { r = &full; n = 1; }
    pvr_wait_render_done();
    front = (uintptr_t)pvr_get_front_buffer();
    /* Read through the cached P1 alias of the same VRAM (burst line fills; the uncached P2 window took 109 ms
       a frame on hardware), invalidating each span first so no stale lines are read. */
    src = (const uint8_t *)(((PVR_RAM_BASE | ((front - PVR_RAM_BASE) >> 1)) & 0x1FFFFFFFu) | 0x80000000u);
    for (int i = 0; i < n; i++) {
        /* one pixel of margin for the scaler's line filter; 16-pixel (32-byte) column alignment for the cache */
        int x0 = (r[i].x0 - 1) & ~15, x1 = (r[i].x1 + 16) & ~15, y0 = r[i].y0 - 1, y1 = r[i].y1 + 1;
        if (x0 < 0) x0 = 0;
        if (x1 > 640) x1 = 640;
        if (y0 < 0) y0 = 0;
        if (y1 > 480) y1 = 480;
        for (int y = y0; y < y1 && x1 > x0; y++) {
            dcache_inval_range((uintptr_t)(src + (y * 640 + x0) * 2), (size_t)(x1 - x0) * 2);
            shz_memcpy(dst + (y * PERSIST_W + x0) * 2, src + (y * 640 + x0) * 2, (size_t)(x1 - x0) * 2);
        }
    }
}

/* Textured strips already copied into the persistent picture (screen box + texture word): album text is redrawn
   every frame in the same place, and copying it again on every cursor move re-filtered its rows each time
   (hardware run 10: "move the menu blur"). Forgotten whenever a frame may have drawn over it. */
#define SEEN_N 1024
static uint32_t seen[SEEN_N];
void gpvr_persist_forget(void) { memset(seen, 0, sizeof seen); }
static __attribute__((unused)) int seen_add(uint32_t h) {           /* 1: newly added (copy it), 0: copied before */
    h |= 1u;
    for (uint32_t i = h & (SEEN_N - 1), k = 0; k < 16; k++, i = (i + 1) & (SEEN_N - 1)) {
        if (seen[i] == h) return 0;
        if (!seen[i]) { seen[i] = h; return 1; }
    }
    return 1;                               /* table crowded: copy it */
}

/* Screen rectangles of the strips in a list so far this frame, from byte offset skip (direct emit only):
   appended to r[n..max); returns the new count, or max + 1 once they do not fit. *u grows to their union.
   Textured strips copied before (seen_add) are left out; textured strips on overlapping rows merge into one
   rectangle (a line of text), untextured ones stay apart (the cursor's four border bars must not become the
   whole photo they frame). */
int gpvr_list_rects(int list, size_t skip, gpvr_rect_t *r, int n, int max, gpvr_rect_t *u) {
#if WINGS64_DIRECT_EMIT
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    uint32_t tex = 0;
    for (const uint8_t *p = wbase[list] + skip; p < wp[list]; p += 32) {
        const pvr_vertex_t *v = (const pvr_vertex_t *)p;
        if ((v->flags & 0xE0000000u) != PVR_CMD_VERTEX) {
            const uint32_t *h = (const uint32_t *)p;
            tex = (h[0] & PVR_TA_CMD_TXRENABLE) ? h[3] : 0;   /* textured: header word 3 is the texture */
            bx0 = by0 = 1e9f; bx1 = by1 = -1e9f;
            continue;
        }
        if (v->x < bx0) bx0 = v->x;
        if (v->x > bx1) bx1 = v->x;
        if (v->y < by0) by0 = v->y;
        if (v->y > by1) by1 = v->y;
        if (v->flags == PVR_CMD_VERTEX_EOL) {
            gpvr_rect_t b;
            b.x0 = (int16_t)(bx0 < 0.0f ? 0 : bx0 > 640.0f ? 640 : bx0);
            b.x1 = (int16_t)(bx1 < 0.0f ? 0 : bx1 > 639.0f ? 640 : bx1 + 1.0f);
            b.y0 = (int16_t)(by0 < 0.0f ? 0 : by0 > 480.0f ? 480 : by0);
            b.y1 = (int16_t)(by1 < 0.0f ? 0 : by1 > 479.0f ? 480 : by1 + 1.0f);
            b.tex = tex != 0;
            bx0 = by0 = 1e9f; bx1 = by1 = -1e9f;
            if (b.x1 <= b.x0 || b.y1 <= b.y0) continue;
            if (tex && !seen_add(((uint32_t)b.x0 * 2654435761u) ^ ((uint32_t)b.y0 * 40503u) ^ ((uint32_t)b.x1 << 20)
                                 ^ ((uint32_t)b.y1 << 10) ^ tex * 2246822519u))
                continue;
            if (b.x0 < u->x0) u->x0 = b.x0;
            if (b.y0 < u->y0) u->y0 = b.y0;
            if (b.x1 > u->x1) u->x1 = b.x1;
            if (b.y1 > u->y1) u->y1 = b.y1;
            if (b.tex) {
                int k;
                for (k = 0; k < n && k < max; k++)
                    if (r[k].tex && r[k].y0 < b.y1 && b.y0 < r[k].y1 && r[k].x0 < b.x1 + 32 && b.x0 < r[k].x1 + 32) break;
                if (k < n && k < max) {
                    if (b.x0 < r[k].x0) r[k].x0 = b.x0;
                    if (b.y0 < r[k].y0) r[k].y0 = b.y0;
                    if (b.x1 > r[k].x1) r[k].x1 = b.x1;
                    if (b.y1 > r[k].y1) r[k].y1 = b.y1;
                    continue;
                }
            }
            if (n < max) r[n] = b;
            if (n <= max) n++;
        }
    }
    return n;
#else
    (void)list; (void)skip; (void)r; (void)max;
    u->x0 = u->y0 = 0; u->x1 = 640; u->y1 = 480;
    return max + 1;
#endif
}

/* Hash of a list's screen footprint so far this frame: vertex x and y only (direct emit; its size otherwise).
   The persistent frame's capture policy uses it to tell apart frames that draw the same amount in different
   places (album cursor moves) while ignoring changes in colour, texture or depth (the pulsing "view photo" text,
   which otherwise re-captured every frame: run 8). */
uint32_t gpvr_list_hash(int list) {
#if WINGS64_DIRECT_EMIT
    uint32_t h = 2166136261u;
    for (const uint32_t *p = (const uint32_t *)wbase[list], *e = (const uint32_t *)wp[list]; p < e; p += 8)
        if ((p[0] & 0xE0000000u) == PVR_CMD_VERTEX) { h = (h ^ p[1]) * 16777619u; h = (h ^ p[2]) * 16777619u; }
    return h;
#else
    return (uint32_t)used[list];
#endif
}

/* Bytes written to a list so far this frame (direct emit; 0 otherwise). */
size_t gpvr_list_bytes(int list) {
#if WINGS64_DIRECT_EMIT
    return (size_t)(wp[list] - wbase[list]);
#else
    return used[list];
#endif
}

void gpvr_frame_end(void) {
#if WINGS64_DIRECT_EMIT
    for (int i = 0; i < 3; i++) {
        if (wp[i] != wbase[i]) pvr_vertbuf_written(list_id[i], (size_t)(wp[i] - wbase[i]));
        last_base[i] = wbase[i]; last_len[i] = (size_t)(wp[i] - wbase[i]);
    }
#endif
#if WINGS64_SHADOW_MOD
    if (mwp == mbase) {
        /* An enabled list must hold something; KOS's blank filler is a polygon header, not a volume. */
        static const float empty[9] = { -64.0f, -64.0f, 1e-6f, -63.0f, -64.0f, 1e-6f, -64.0f, -63.0f, 1e-6f };
        gpvr_mod_volume(empty, 1);
    }
    pvr_vertbuf_written(PVR_LIST_OP_MOD, (size_t)(mwp - mbase));
#if WINGS64_DIRECT_EMIT
    last_base[3] = mbase; last_len[3] = (size_t)(mwp - mbase);
#endif
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
       act on the OP and PT lists). Screen-space layers (HUD, sky) stay as they are. TR polygons never get the
       modifier bit: the frame has no TR modifier list, and on hardware the translucent sea came out wrong
       with it set (hardware run 4; Flycast ignored it). */
    if (st->depth_cmp != PVR_DEPTHCMP_ALWAYS && st->list != GPVR_TR && !gpvr_mod_off) {
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
