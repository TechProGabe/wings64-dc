/* dcpvr.h — PVR bring-up and the VRAM fence (ssb64-dc dcpvr.h pattern). */
#ifndef WINGS64_DCPVR_H
#define WINGS64_DCPVR_H

#include <dc/pvr.h>

#ifndef WINGS64_SHADOW_MOD
#define WINGS64_SHADOW_MOD 1   /* shadow volumes as OP modifier volumes (gfx_pvr.h) */
#endif

/* Punch-through alpha reference (0xA05F811C). KOS v2.2.2 names no constant for it. */
#ifndef PVR_PT_ALPHA_REF
#define PVR_PT_ALPHA_REF 0x011c
#endif

/* OPB bins for the OP, OP modifier (shadow volumes, WINGS64_SHADOW_MOD), TR, PT lists. 512 KB vertex buffer
   (KOS keeps two: 1 MB VRAM).
   WINGS64_OPB_BIG (default 1): every triangle is its own strip, so each takes one object pointer in every tile
   it touches; layered full-screen sky/sea/terrain filled 16-pointer bins and the 3-set overflow pool ran out
   about every other frame on hardware (LOG004: 5707 OPB out-of-memory events in 378 s, from the title on;
   the TA then drops the rest of the list for that tile: the sea vanished, things popped). OP and TR bins go to
   32 pointers and the pool to 7 sets: ~0.9 MB per buffer half instead of ~0.3 MB. Kill switch: =0. */
#ifndef WINGS64_OPB_BIG
#define WINGS64_OPB_BIG 1
#endif
#if WINGS64_OPB_BIG
#define DC_OPB_MAIN PVR_BINSIZE_32
#define DC_OPB_OVERFLOW 7
#else
#define DC_OPB_MAIN PVR_BINSIZE_16
#define DC_OPB_OVERFLOW 3
#endif
static inline void dc_pvr_init(void) {
    pvr_init_params_t p = {
#if WINGS64_SHADOW_MOD
        { DC_OPB_MAIN, PVR_BINSIZE_16, DC_OPB_MAIN, PVR_BINSIZE_0, PVR_BINSIZE_16 },   /* + OP modifiers */
#else
        { DC_OPB_MAIN, PVR_BINSIZE_0, DC_OPB_MAIN, PVR_BINSIZE_0, PVR_BINSIZE_16 },
#endif
        512 * 1024, /* vertex_buf_size */
        1,          /* dma_enabled: lists fill RAM buffers in any order (gfx_pvr.c) */
        0,          /* fsaa_enabled */
        1,          /* autosort_disabled: translucent polys draw in N64 submission order */
        DC_OPB_OVERFLOW, /* opb_overflow_count */
        0,          /* vbuf_doublebuf_disabled */
    };
    pvr_init(&p);
    PVR_SET(PVR_PT_ALPHA_REF, 0x80);
}

/* Call before every VRAM texture upload or free (tools/check/vramfence_check.py, M2). */
static inline void dc_pvr_vram_fence(void) { pvr_wait_render_done(); }

#endif
