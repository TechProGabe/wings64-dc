/* splash.c — "TECHPROGABE presents", shown once per boot before the title screen (patches/025).
 * DIVERGES: port splash (not on the N64; the N64 goes straight from the logo to the title).
 *
 * Built from the game's own parts, like the settings panel (settings.c):
 *  - backdrop: options_80316B80() loads the Option screen's level and its sky sprite; optionsDrawBorder()
 *    draws it with the black letterbox the Option screen uses;
 *  - text: font 6, the bold alphabet (the text tables' set; see kb/traps.md "Port settings menu"), with
 *    the title screen's white and the menus' yellow;
 *  - fades: fade-in is a black quad over the frame (the same vertex pattern as func_8032D51C), fade-out
 *    is func_8032D51C(0) itself, as at every other screen change in the game.
 * Skipped with A or Start. About 3 s with no skip.
 * Kill switch: WINGS64_SPLASH (src/dc/decomp/wings64_splash.h).
 */
#include <ultra64.h>
#include "common.h"
#include <uv_font.h>
#include <uv_geometry.h>
#include <uv_graphics.h>
#include <uv_sprite.h>
#include <uv_string.h>
#include "app/demo.h"
#include "app/game.h"
#include "app/replay_screen.h"
#include "app/snd.h"
#include "wings64_splash.h"
#include "ultra/kos_bridge.h"

#if WINGS64_SPLASH

/* Game code with no header (src/app/options.c, src/app/code_9A960.c). */
void options_80316B80(void);
void optionsDrawBorder(void);
void func_80314154(void);
void func_803141E4(void);

#define SPLASH_FONT 6        /* the title and Option menus' font */
#define FADE_IN_FRAMES 24    /* ~0.4 s from black */
#define HOLD_FRAMES 96       /* ~1.6 s with the text up */
#ifndef WINGS64_SPLASH_SFX
#define WINGS64_SPLASH_SFX SFX_QUICK_SWISH /* the title intro's "64" logo fly-in: a short reveal swish */
#endif
#define CAP 40               /* glyph string capacity, terminator included */

#define BRAND_Y 115          /* screen y in the 320 x 240 space; font 6 counts y upwards from the bottom */
#define PRESENTS_Y 81

static s16 sBrand[CAP];
static s16 sPresents[CAP];
static s32 sBrandW, sPresentsW;

/* Where the bold alphabet starts in font 6's STRG (the thin set comes first; see settings.c). */
static s32 bold_base(const char *str) {
    s32 i, base = 0;
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] == 'A' && i >= 10 && str[i - 10] == '0' && str[i - 1] == '9') base = i - 10;
    }
    return base;
}

/* ASCII -> glyph indices of the bold set, and the string's width in pixels at scale 1. Unknown characters
   are logged (the font's glyphs are not all known to the port) and drawn as a blank cell. */
static s32 to_glyphs(s16 *dst, const char *s, s32 *width) {
    ParsedUVFT *f = gGfxUnkPtrs->fonts[SPLASH_FONT];
    s32 base = bold_base(f->str);
    s32 at = 0, w = 0;
    for (; *s != '\0' && at < CAP - 1; s++) {
        char *p = uvStrchr(f->str + base, *s);
        if (p == NULL) {
            kb_log("splash: no bold glyph for '%c'\n", *s);
            dst[at++] = -2;
            w += 6;
            continue;
        }
        dst[at] = (s16)(p - f->str);
        w += f->bitmap[dst[at]].width;
        at++;
    }
    dst[at] = -1;
    *width = w;
    return at;
}

/* Black over the whole frame at alpha a (0 clear, 255 black), drawn over the text as well. */
static void fade_black(u8 a) {
    func_80314154();
    uvGfxSetFlags(GFX_STATE_AA);
    uvVtxBeginPoly();
    uvVtx(-1, -1, 0, 0, 0, 0, 0, 0, a);
    uvVtx(SCREEN_WIDTH, -1, 0, 0, 0, 0, 0, 0, a);
    uvVtx(SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, 0, 0, 0, a);
    uvVtx(-1, SCREEN_HEIGHT, 0, 0, 0, 0, 0, 0, a);
    uvVtxEndPoly();
    func_803141E4();
}

static void draw_text(s16 *str, s32 w, s32 y, u8 r, u8 g, u8 b) {
    uvFontSet(SPLASH_FONT);
    uvFontScale(1.0f, 1.0f);
    uvFontColor(r, g, b, 0xFF);
    uvFontPrintStr16((SCREEN_WIDTH - w) / 2, y, str, CAP, 0xFFE);
}

/* A or Start this frame (demoButtonPress reports the press edge). */
static s32 skip_pressed(void) {
    return demoButtonPress(D_80362690->unk9C, A_BUTTON | START_BUTTON);
}

int wings64_splash_run(void) {
    static u8 sDone;
    s32 f, skipped = 0;

    if (sDone) return 0;
    sDone = 1;

    options_80316B80();
    to_glyphs(sBrand, "TECHPROGABE", &sBrandW);
    to_glyphs(sPresents, "presents", &sPresentsW);

    for (f = 0; f < FADE_IN_FRAMES + HOLD_FRAMES; f++) {
        if (skip_pressed()) {
            skipped = 1;
            break;
        }
        if (f == 1) sndPlaySfx(WINGS64_SPLASH_SFX); /* once, as the text comes up out of the fade */
        uvGfxBegin();
        optionsDrawBorder();
        draw_text(sBrand, sBrandW, BRAND_Y, 0xFF, 0xFF, 0xFF);
        draw_text(sPresents, sPresentsW, PRESENTS_Y, 0xFF, 0xFF, 0x00);
        uvFontGenDlist();
        if (f < FADE_IN_FRAMES) fade_black((u8)(255 * (FADE_IN_FRAMES - f) / FADE_IN_FRAMES));
        uvGfxEnd();
    }
    func_8032D51C(0);
    kb_log("splash: %s\n", skipped ? "skipped" : "shown");
    return 1;
}

#endif /* WINGS64_SPLASH */
