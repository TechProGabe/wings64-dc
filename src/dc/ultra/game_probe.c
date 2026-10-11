/* game_probe.c — read-only view of the game's state machine for logs and the emulator gate.
 * Compiled with the decomp flags (needs app/game.h); main.c only sees the int. */
#include <ultra64.h>
#include "app/game.h"

s32 wings64_game_state(void) {
    return D_80362690 != NULL ? (s32)D_80362690->state : -1;
}

#include <uv_graphics.h>
#include <uv_texture.h>
void db_log(const char *fmt, ...);
/* Diagnostics (called by main.c only in DB_GFX_TRACE builds). One-time dump of model scale factors (uvmd->unk20) for every loaded model whose scale is not 1. */
void wings64_dump_model_scales(void) {
    s32 i, n = 0;
    if (gGfxUnkPtrs == NULL) return;
    for (i = 0; i < 0x190; i++) {
        ParsedUVMD *m = gGfxUnkPtrs->models[i];
        if (m != NULL && m->unk20 != 1.0f && n++ < 40) db_log("model %03x scale %d/1000\n", (int)i, (int)(m->unk20 * 1000.0f));
    }
    db_log("models with scale != 1: %d\n", (int)n);
}
