/* dcpvr.h — PVR bring-up and the VRAM fence (ssb64-dc dcpvr.h pattern). */
#ifndef WINGS64_DCPVR_H
#define WINGS64_DCPVR_H

#include <dc/pvr.h>

/* Punch-through alpha reference (0xA05F811C). KOS v2.2.2 names no constant for it. */
#ifndef PVR_PT_ALPHA_REF
#define PVR_PT_ALPHA_REF 0x011c
#endif

/* OPB {16,0,16,0,16}: OP, PT, TR lists; no modifier volumes. 512 KB vertex buffer
   (KOS keeps two: 1 MB VRAM). 3 overflow OPBs against tile-edge dropouts. */
static inline void dc_pvr_init(void) {
    pvr_init_params_t p = {
        { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16 },
        512 * 1024, /* vertex_buf_size */
        1,          /* dma_enabled: lists fill RAM buffers in any order (gfx_pvr.c) */
        0,          /* fsaa_enabled */
        1,          /* autosort_disabled: translucent polys draw in N64 submission order */
        3,          /* opb_overflow_count */
        0,          /* vbuf_doublebuf_disabled */
    };
    pvr_init(&p);
    PVR_SET(PVR_PT_ALPHA_REF, 0x80);
}

/* Call before every VRAM texture upload or free (tools/check/vramfence_check.py, M2). */
static inline void dc_pvr_vram_fence(void) { pvr_wait_render_done(); }

#endif
