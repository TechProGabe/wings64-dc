/* autoplay.c — scripted controller input for the emulator gate (DB_AUTOPLAY, off by default).
 * os_shim.c calls wings64_autoplay() for port A on every controller read; when a script step is
 * active it replaces the buttons. Steps are timed from the first controller read, in milliseconds.
 * Script (DB_AUTOPLAY_SCRIPT): "ms:BUTTONS:hold_ms,..." with BUTTONS from S(tart) A B Z L R U D l r
 * (U/D/l/r = D-pad up/down/left/right; > < ^ v = analog stick full right/left/up/down).
 * Example: "10000:S:150,14000:A:150,20000:>:300".
 */
#include "db.h"

#include <kos.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if DB_AUTOPLAY
#ifndef DB_AUTOPLAY_SCRIPT
#define DB_AUTOPLAY_SCRIPT "10000:S:150,14000:A:150,18000:A:150,22000:A:150,26000:A:150,30000:A:150,34000:A:150"
#endif

/* N64 button bits (PR/os_cont.h) */
#define N_A 0x8000
#define N_B 0x4000
#define N_Z 0x2000
#define N_START 0x1000
#define N_UP 0x0800
#define N_DOWN 0x0400
#define N_LEFT 0x0200
#define N_RIGHT 0x0100
#define N_L 0x0020
#define N_R 0x0010

typedef struct { uint32_t at, hold; uint16_t buttons; int8_t sx, sy; } step_t;
static step_t steps[64];
static int nsteps = -1;
static uint64_t t0;

static void parse(void) {
    const char *p = DB_AUTOPLAY_SCRIPT;
    nsteps = 0;
    while (*p && nsteps < 64) {
        step_t s = { 0 };
        s.at = (uint32_t)strtoul(p, (char **)&p, 10);
        if (*p == ':') p++;
        for (; *p && *p != ':'; p++) {
            switch (*p) {
            case 'S': s.buttons |= N_START; break; case 'A': s.buttons |= N_A; break;
            case 'B': s.buttons |= N_B; break;     case 'Z': s.buttons |= N_Z; break;
            case 'L': s.buttons |= N_L; break;     case 'R': s.buttons |= N_R; break;
            case 'U': s.buttons |= N_UP; break;    case 'D': s.buttons |= N_DOWN; break;
            case 'l': s.buttons |= N_LEFT; break;  case 'r': s.buttons |= N_RIGHT; break;
            case '>': s.sx = 80; break;            case '<': s.sx = -80; break;
            case '^': s.sy = 80; break;            case 'v': s.sy = -80; break;
            }
        }
        if (*p == ':') p++;
        s.hold = (uint32_t)strtoul(p, (char **)&p, 10);
        steps[nsteps++] = s;
        while (*p == ',' || *p == ' ') p++;
    }
}

int wings64_autoplay(uint32_t poll, uint16_t *buttons, int8_t *sx, int8_t *sy) {
    uint64_t now;
    (void)poll;
    if (nsteps < 0) { parse(); t0 = timer_ms_gettime64(); }
    now = timer_ms_gettime64() - t0;
    for (int i = 0; i < nsteps; i++) {
        if (now >= steps[i].at && now < (uint64_t)steps[i].at + steps[i].hold) {
            static int logged[64];
            if (!logged[i]) {
                logged[i] = 1;
                db_log("autoplay: t %lu ms buttons %04x stick %d,%d\n", (unsigned long)now, steps[i].buttons, steps[i].sx, steps[i].sy);
            }
            *buttons = steps[i].buttons;
            if (steps[i].sx) *sx = steps[i].sx;
            if (steps[i].sy) *sy = steps[i].sy;
            return 1;
        }
    }
    return 0;
}
#endif
