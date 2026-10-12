/* screen.c — text overlay rendered through the PVR (exercises pvr_init, texture upload,
 * OP list submission and the scene loop that M2's Fast3D backend will use). */
#include "screen.h"
#include "dcpvr.h"

#include <kos.h>
#include <dc/biosfont.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <sh4zam/shz_sh4zam.h>

#define TEX_W 1024
#define TEX_H 512
#define MARGIN_X 24
#define MARGIN_Y 24

static uint16_t *pixels;            /* RAM copy, 32-byte aligned for pvr_txr_load's SQ path */
static pvr_ptr_t tex;
static pvr_poly_hdr_t hdr;
static int dirty;

void screen_init(void) {
    pvr_poly_cxt_t cxt;
    pixels = memalign(32, TEX_W * TEX_H * 2);
    shz_memset8(pixels, 0, TEX_W * TEX_H * 2);
    tex = pvr_mem_malloc(TEX_W * TEX_H * 2);
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                     TEX_W, TEX_H, tex, PVR_FILTER_NONE);
    pvr_poly_compile(&hdr, &cxt);
    pvr_set_bg_color(0.0f, 0.0f, 0.0f);
    dirty = 1;
}

void screen_line(int row, uint16_t color, const char *fmt, ...) {
    char text[64];
    va_list ap;
    uint16_t *dst;
    if (row < 0 || row >= SCREEN_ROWS) return;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    dst = pixels + (MARGIN_Y + row * BFONT_HEIGHT) * TEX_W + MARGIN_X;
    /* Clear the row (592 px = 49 glyphs) before drawing the new text. */
    for (int y = 0; y < BFONT_HEIGHT; y++)
        shz_memset8(dst + y * TEX_W, 0, (640 - 2 * MARGIN_X) * 2);
    bfont_draw_str_ex(dst, TEX_W, color, 0x0000, 16, true, text);
    dirty = 1;
}

#define BAR_X 192
#define BAR_Y 404
#define BAR_W 256
#define BAR_H 4

void screen_loading(void) {
    bfont_draw_str_ex(pixels + (BAR_Y - 28) * TEX_W + BAR_X, TEX_W, SCREEN_GREY, 0x0000, 16, true, "Loading");
    screen_bar(0);
}

void screen_bar(int pct) {
    uint16_t *dst = pixels + BAR_Y * TEX_W + BAR_X;
    int filled = (pct < 0 ? 0 : pct > 100 ? 100 : pct) * BAR_W / 100;
    for (int y = 0; y < BAR_H; y++)
        for (int x = 0; x < BAR_W; x++)
            dst[y * TEX_W + x] = x < filled ? 0xFFE0 : 0x2104;   /* yellow over a dim track */
    dirty = 1;
}

static void quad(void) {
    pvr_vertex_t v;
    const float u1 = 640.0f / TEX_W, v1 = 480.0f / TEX_H;
    v.flags = PVR_CMD_VERTEX; v.argb = 0xFFFFFFFF; v.oargb = 0; v.z = 1.0f;
    v.x = 0.0f;   v.y = 480.0f; v.u = 0.0f; v.v = v1; pvr_list_prim(PVR_LIST_OP_POLY, &v, sizeof v);
    v.x = 0.0f;   v.y = 0.0f;   v.u = 0.0f; v.v = 0.0f; pvr_list_prim(PVR_LIST_OP_POLY, &v, sizeof v);
    v.x = 640.0f; v.y = 480.0f; v.u = u1;   v.v = v1; pvr_list_prim(PVR_LIST_OP_POLY, &v, sizeof v);
    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = 640.0f; v.y = 0.0f;   v.u = u1;   v.v = 0.0f; pvr_list_prim(PVR_LIST_OP_POLY, &v, sizeof v);
}

void screen_frame(void) {
    pvr_wait_ready();
    if (dirty) {
        dc_pvr_vram_fence();
        pvr_txr_load(pixels, tex, TEX_W * TEX_H * 2);
        dirty = 0;
    }
    pvr_scene_begin();
    pvr_list_prim(PVR_LIST_OP_POLY, &hdr, sizeof hdr);   /* vertex DMA mode (dcpvr.h) */
    quad();
    pvr_scene_finish();
}
