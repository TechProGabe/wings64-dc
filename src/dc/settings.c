/* settings.c — the port settings menu: a third title-screen item, "Settings", that opens a panel drawn
 * and driven exactly like the game's own Option screen, plus the FPS monitor it switches.
 * DIVERGES: port settings menu (title item, settings panel, FPS overlay, VMU-persisted flags; none exist on the N64)
 *
 * Compiled with the decomp flags (DECOMPLIKE_SRC) so it can call the game's UI code directly:
 *  - backdrop: options_80316B80() + optionsDrawBorder(), the Option screen's own level, sprite 0 and
 *    black overscan border;
 *  - rows: menuCreateItems / menuSetItem / menuCheckInputs / menuRender, i.e. menu_utils.c draws the
 *    yellow triangle cursor, white items and yellow selection in font 6, plays the move chime, cancel
 *    (B) and confirm sounds, and reads the stick with the game's 0.75 threshold;
 *  - values change like the Sound panel's Stereo/Mono row (options.c optionsHandlerSound): stick
 *    left/right (and here also A and the D-pad) flip the value with sound 0x54;
 *  - text is the game's: row labels are built from font 6's own glyph table (STRG), "Return" is
 *    TEXT_QUIT from the text block, and values sit in a column through the font's 0xFD x-position code;
 *  - leaving fades out with func_8032D51C(0) like the Option screen, and the title comes back without
 *    the intro (game.c's GAME_STATE_OPTIONS path, unchanged).
 * Persistence: one 8-byte block at EEPROM offset 0x7F8 (os_shim.c), which the game can never touch:
 * uvFileRead/uvFileWrite (kernel/system.c, the only callers of osEepromLong*) refuse offs+nbytes > 0x208.
 * The block is saved to the VMU with the game's EEPROM image (src/dc/save/vmu_eeprom.c).
 * Kill switch: WINGS64_SETTINGS_MENU=0 (src/dc/decomp/wings64_settings.h).
 */
#include <ultra64.h>
#include "common.h"
#include <uv_font.h>
#include <uv_graphics.h>
#include <uv_string.h>
#include "app/demo.h"
#include "app/game.h"
#include "app/menu.h"
#include "app/menu_utils.h"
#include "app/replay_screen.h"
#include "app/snd.h"
#include "app/text_data.h"
#include "wings64_settings.h"
#include "ultra/kos_bridge.h"

#if WINGS64_SETTINGS_MENU

/* Game code with no header (src/app/options.c). */
void options_80316B80(void);
void optionsDrawBorder(void);

/* Port side. */
u32 wings64_gfx_tasks(void);
int wings64_eeprom_port_read(unsigned char block[8]);
int wings64_eeprom_port_write(const unsigned char block[8]);
int db_sd_available(void);
int db_sd_enabled(void);
void db_sd_set_enabled(int on);
const char *db_sd_status(void);

#define MENU_FONT 6        /* title and Option menus */
#define SFX_OPTION_FLIP 0x54 /* options.c: Stereo/Mono, Sound Track changes */

#define FLAG_FPS 0x01
#define FLAG_SD 0x02
#define BLOCK_VERSION 1
/* FPS monitor state when the VMU holds no saved settings (./run.sh hwdisc builds hardware-test discs with 1). */
#ifndef WINGS64_FPS_DEFAULT
#define WINGS64_FPS_DEFAULT 0
#endif

static u8 sLoaded;
static u8 sFlags;          /* persisted FLAG_* */
static u8 sRequested;      /* title chose "Settings" */
static s16 sTitleLabel[16];

/* ------------------------------------------------------------------ persistence */

static void settings_load(void) {
    unsigned char b[8];
    if (sLoaded) return;
    sLoaded = 1;
    sFlags = WINGS64_FPS_DEFAULT ? FLAG_FPS : 0;
    if (wings64_eeprom_port_read(b) && b[0] == 'W' && b[1] == '6' && b[2] == '4' && b[3] == 'S' &&
        b[4] == BLOCK_VERSION && (u8)(b[5] ^ b[6]) == 0xFF) {
        sFlags = b[5];
        kb_log("settings: loaded fps monitor %s, SD log %s\n", (sFlags & FLAG_FPS) ? "on" : "off",
               (sFlags & FLAG_SD) ? "on" : "off");
    }
    /* SD at boot = build default (DB_LOG_SD) or the saved switch. Only ever switched on here: the card is
       brought up by the main thread's db_sd_poll after DB_LOG_SD_ARM_MS, as with DB_LOG_SD=1. */
    if ((sFlags & FLAG_SD) && db_sd_available() && !db_sd_enabled()) db_sd_set_enabled(1);
}

static void settings_save(void) {
    unsigned char b[8];
    int n;
    b[0] = 'W'; b[1] = '6'; b[2] = '4'; b[3] = 'S';
    b[4] = BLOCK_VERSION;
    b[5] = sFlags;
    b[6] = (u8)(sFlags ^ 0xFF);
    b[7] = 0;
    n = wings64_eeprom_port_write(b);
    kb_log("settings: fps monitor %s, SD log %s, %s\n", (sFlags & FLAG_FPS) ? "on" : "off",
           (sFlags & FLAG_SD) ? "on" : "off", n ? "saved to VMU" : "not saved (no VMU)");
}

/* ------------------------------------------------------------------ glyph strings */

/* Where the game's text glyphs start in a font's STRG (ASCII -> bitmap index table). Font 6 carries two
   alphabets: a thin one first, then the bold one the text tables use ("Option" = 0x78 0x93 ... in
   menu.c is 'O' and 'p' of the second set). uvFontPrintStr would pick the first, so look from the last
   "0123456789A" run instead. */
static s32 text_set_base(const char *str) {
    s32 i, base = 0;
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] == 'A' && i >= 10 && str[i - 10] == '0' && str[i - 1] == '9') base = i - 10;
    }
    return base;
}

/* ASCII -> 16-bit glyph string of a loaded font, as the game's text tables encode it (STRG position =
   bitmap index; unknown characters become -2, drawn as a blank cell like uvFontPrintStr does). */
static s32 glyphs(s16 *dst, s32 at, s32 cap, s32 font, const char *s) {
    ParsedUVFT *f = gGfxUnkPtrs->fonts[font];
    s32 base = f != NULL ? text_set_base(f->str) : 0;
    for (; *s != '\0' && at < cap - 1; s++) {
        char *p = NULL;
        if (f != NULL) {
            p = uvStrchr(f->str + base, *s);
            if (p == NULL) p = uvStrchr(f->str, *s);
        }
        dst[at++] = p != NULL ? (s16)(p - f->str) : -2;
    }
    dst[at] = -1;
    return at;
}

/* 0xFD x y: uvFontPrintStr16 (US) starts a new run at screen x. Used for the value column. */
static s32 column(s16 *dst, s32 at, s32 cap, s32 x) {
    if (at + 3 >= cap) return at;
    dst[at++] = 0xFD;
    dst[at++] = (s16)x;
    dst[at++] = 0;
    dst[at] = -1;
    return at;
}

/* ------------------------------------------------------------------ title screen */

int wings64_settings_title_init(void) {
    settings_load();
    uvFontSet(MENU_FONT);
    uvFontScale(1.0f, 1.0f);
    glyphs(sTitleLabel, 0, ARRAY_COUNT(sTitleLabel), MENU_FONT, "Settings");
    return uvFontHeight() + 1;
}

short *wings64_settings_title_label(void) {
    return sTitleLabel;
}

int wings64_settings_open(void) {
    sRequested = 1;
    return GAME_STATE_OPTIONS;
}

int wings64_settings_take_request(void) {
    int r = sRequested;
    sRequested = 0;
    return r;
}

/* ------------------------------------------------------------------ the panel */

enum { ROW_FPS, ROW_SD, ROW_RETURN };

#define PANEL_X 80         /* optionsInitMain: menuCreateItems(80, ...) */
#define VALUE_X 196        /* value column (screen x of "On"/"Off") */
#define ROW_CAP 40

static s32 sPanelItems[3];
static s32 sPanelRowKind[3]; /* ROW_* of each menu index */
static s32 sPanelCount;
static s16 sRowText[2][ROW_CAP];
static s32 sStickHeld;
static u8 sDirty;
static s32 sPanelY;
static s32 sPanelTextX;

static void panel_set_row(s32 idx) {
    s32 n;
    s32 kind = sPanelRowKind[idx];
    s32 on = kind == ROW_FPS ? (sFlags & FLAG_FPS) != 0 : db_sd_enabled();
    n = glyphs(sRowText[idx], 0, ROW_CAP, MENU_FONT, kind == ROW_FPS ? "FPS monitor" : "SD card log");
    n = column(sRowText[idx], n, ROW_CAP, VALUE_X);
    glyphs(sRowText[idx], n, ROW_CAP, MENU_FONT, on ? "On" : "Off");
    menuSetItem(idx, sRowText[idx]);
}

static void panel_init(void) {
    s32 i, y;

    sPanelCount = 0;
    sPanelRowKind[sPanelCount++] = ROW_FPS;
    if (db_sd_available()) sPanelRowKind[sPanelCount++] = ROW_SD; /* release builds: no SD logger */
    sPanelRowKind[sPanelCount++] = ROW_RETURN;
    for (i = 0; i < sPanelCount; i++) sPanelItems[i] = TEXT_QUIT; /* rows other than Return replaced below */
    /* optionsInitMain centres 3 rows at y 100; one row is 17 px */
    y = 100 + (3 - sPanelCount) * 9;
    menuCreateItems(PANEL_X, y, MENU_FONT, 1.0f, 1.0f, sPanelItems, sPanelCount);
    sPanelY = y;
    sPanelTextX = PANEL_X + uvFontWidth("A") / 2 + 3; /* where menuUtilRender starts item text */
    menuUtilSetColors(MENU_COLOR_ITEM, 0xFF, 0xFF, 0xFF);
    menuUtilSetColors(MENU_COLOR_SELECTED, 0xFF, 0xFF, 0x00);
    for (i = 0; i < sPanelCount; i++) {
        if (sPanelRowKind[i] != ROW_RETURN) panel_set_row(i);
    }
    menuUtilSetSoundFlags(MENU_SOUND_CHANGE | MENU_SOUND_BACK); /* as the Sound panel: A is handled here */
    sStickHeld = 0;
    sDirty = 0;
}

static void panel_flip(s32 idx) {
    s32 kind = sPanelRowKind[idx];
    if (kind == ROW_FPS) {
        sFlags ^= FLAG_FPS;
    } else if (kind == ROW_SD) {
        db_sd_set_enabled(!db_sd_enabled()); /* the main thread brings the card up / down at its next poll */
        sFlags = (u8)((sFlags & ~FLAG_SD) | (db_sd_enabled() ? FLAG_SD : 0));
    } else {
        return;
    }
    sDirty = 1;
    sndPlaySfx(SFX_OPTION_FLIP);
    panel_set_row(idx);
}

/* D-pad up/down: same move, wrap and chime as the stick in menuUtilCheckInputs. */
static void panel_dpad_move(s32 cur) {
    s32 next = cur;
    if (demoButtonPress(D_80362690->unk9C, U_JPAD)) next = cur - 1;
    if (demoButtonPress(D_80362690->unk9C, D_JPAD)) next = cur + 1;
    if (next == cur) return;
    if (next < 0) next = sPanelCount - 1;
    if (next >= sPanelCount) next = 0;
    menu_8030B69C(next);
    sndPlaySfxVolPitchPan(SFX_UI_MOVE_CHIME, 1.0f, 1.03f, 0.0f);
    func_8033FB14();
}

/* -1 = stay, 0 = leave. Mirrors optionsHandlerSound. */
static s32 panel_update(void) {
    s32 sel, cur, flip;
    f32 stickX;

    sel = menuCheckInputs();
    cur = menu_8030B668();
    if (sel == -1) {
        func_8032D51C(0);
        return 0;
    }
    if (sel >= 0) {
        if (sPanelRowKind[sel] == ROW_RETURN) {
            sndPlaySfx(SFX_UI_CONFIRM);
            func_8032D51C(0);
            return 0;
        }
        panel_flip(sel);
        return -1;
    }
    flip = 0;
    stickX = demoGetInputs(0, INPUT_AXIS_X);
    if (ABS_NOEQ(stickX) < 0.75f) {
        sStickHeld = FALSE;
    } else if (!sStickHeld) {
        flip = 1;
        sStickHeld = TRUE;
    }
    if (demoButtonPress(D_80362690->unk9C, L_JPAD | R_JPAD)) flip = 1;
    if (flip) panel_flip(cur);
    panel_dpad_move(cur);
    return -1;
}

/* SD logger state under the menu while it is switched on (or still shutting down), in the menu's font
   and item colour: "SD card  Probing", "SD card  LOG003.TXT". It changes on the main thread
   (db_sd_poll), so it is rebuilt every frame. Sentence case like the game's text, file names as is. */
static void panel_draw_status(void) {
    static s16 line[ROW_CAP];
    char st[24];
    s32 n, y;

    if (!db_sd_available()) return;
    uvSprintf(st, "%s", db_sd_status());
    if (!db_sd_enabled() && uvStrcmp(st, "turning off") != 0) return;
    if (st[0] >= 'a' && st[0] <= 'z' && !(st[0] == 'l' && st[1] == 'o' && st[2] == 'g' && st[3] == '-')) st[0] -= 'a' - 'A';
    n = glyphs(line, 0, ROW_CAP, MENU_FONT, "SD card");
    n = column(line, n, ROW_CAP, VALUE_X);
    glyphs(line, n, ROW_CAP, MENU_FONT, st);
    uvFontSet(MENU_FONT);
    uvFontScale(1.0f, 1.0f);
    uvFontColor(0xFF, 0xFF, 0xFF, 0xFF);
    y = sPanelY - 2 * (uvFontHeight() + 1);
    uvFontPrintStr16(sPanelTextX, y, line, 100, 0xFFE);
}

int wings64_settings_screen(void) {
    s32 ret;

    settings_load();
    options_80316B80(); /* the Option screen's level, backdrop sprite and text block */
    panel_init();
    while ((ret = panel_update()) == -1) {
        uvGfxBegin();
        optionsDrawBorder();
        menuRender();
        panel_draw_status();
        uvFontGenDlist();
        uvGfxEnd();
    }
    menuDeinit();
    if (sDirty) settings_save();
    return ret;
}

/* ------------------------------------------------------------------ FPS monitor */

static u32 sFps;
static u32 sFpsTasks0;
static unsigned long long sFpsT0;

/* Presented frames per second over a 1 s window: graphics tasks (one per uvGfxEnd, rendered by
   gfx_f3d_run) counted against the KOS clock. Integer only. */
static void fps_sample(void) {
    unsigned long long now = kb_time_ns();
    u32 tasks = wings64_gfx_tasks();
    unsigned long long dt;

    if (sFpsT0 == 0) {
        sFpsT0 = now;
        sFpsTasks0 = tasks;
        return;
    }
    dt = now - sFpsT0;
    if (dt >= 1000000000ull) {
        sFps = (u32)(((unsigned long long)(tasks - sFpsTasks0) * 1000000000ull + dt / 2) / dt);
        sFpsT0 = now;
        sFpsTasks0 = tasks;
    }
}

#ifdef WINGS64_SETTINGS_FONT_DUMP
static void font_dump(void) {
    static ParsedUVFT *seen[LEVEL_FONT_COUNT];
    s32 i;
    for (i = 0; i < LEVEL_FONT_COUNT; i++) {
        ParsedUVFT *f = gGfxUnkPtrs->fonts[i];
        if (f != seen[i]) {
            seen[i] = f;
            if (f != NULL) kb_log("font %d h %d: [%s]\n", (int)i, (int)f->bitmap[1].actualHeight, f->str);
        }
    }
}
#endif

/* Drawn like the flight HUD's TIME readout: a small yellow label and the HUD number font (font 8, as
   speed / altitude / timer). In flight it sits at the top between the timer and the compass; elsewhere
   in the top-right corner, clear of the centred screen titles. Levels without those fonts fall back to
   the menu font. */
#define FPS_X_FLIGHT 150
#define FPS_X_CORNER 262
#define FPS_TOP 16

void wings64_settings_frame(void) {
    char str[12];
    s32 x, numX;

    settings_load();
    fps_sample();
#ifdef WINGS64_SETTINGS_FONT_DUMP
    font_dump();
#endif
    x = D_80362690 != NULL && D_80362690->state == GAME_STATE_TEST_UPDATE ? FPS_X_FLIGHT : FPS_X_CORNER;
    /* SD log state under the counter while logging is on ("SD PROBE INIT", "SD FAIL MOUNT", "SD LOG000 TXT"):
       the console is muted during the card bring-up, so on hardware the screen is the only witness. */
    if (db_sd_enabled()) {
        char sd[28];
        const char *st = db_sd_status();
        s32 i;
        sd[0] = 'S'; sd[1] = 'D'; sd[2] = ' ';
        for (i = 0; st[i] != '\0' && i < 24; i++) {
            char c = st[i];
            sd[3 + i] = c >= 'a' && c <= 'z' ? (char)(c - 32) : (c == '.' ? ' ' : c);
        }
        sd[3 + i] = '\0';
        /* right-aligned (~8 px per glyph): from the counter's x a long status runs off the 320 px screen */
        uvFontOverlayStr_dc(3, MAX(8, 312 - (3 + i) * 8), SCREEN_HEIGHT - FPS_TOP - 30, 0xFF, 0xFF, 0x00, sd);
    }
    if (!(sFlags & FLAG_FPS) || sFps == 0) return;
    uvSprintf(str, "%d", (int)sFps);
    numX = x;
    /* y is the glyph bottom, counted up from the screen bottom (uvFontPrintStr) */
    if (uvFontOverlayStr_dc(3, x, SCREEN_HEIGHT - FPS_TOP - 11, 0xFF, 0xFF, 0x00, "FPS")) numX += 26;
    if (!uvFontOverlayStr_dc(8, numX, SCREEN_HEIGHT - FPS_TOP - 14, 0xFF, 0xFF, 0xFF, str)) {
        uvFontOverlayStr_dc(MENU_FONT, numX, SCREEN_HEIGHT - FPS_TOP - 17, 0xFF, 0xFF, 0xFF, str);
    }
}

#endif /* WINGS64_SETTINGS_MENU */
