/* autoplay.c — scripted controller input for the emulator gate (DB_AUTOPLAY, off by default).
 * os_shim.c calls wings64_autoplay() for port A on every controller read; when a script step is
 * active it replaces the buttons. Steps are timed from the first controller read, in milliseconds.
 * Script (DB_AUTOPLAY_SCRIPT): "ms:BUTTONS:hold_ms,..." with BUTTONS from S(tart) A B Z L R U D l r
 * (U/D/l/r = D-pad up/down/left/right; > < ^ v = analog stick full right/left/up/down).
 * Example: "10000:S:150,14000:A:150,20000:>:300".
 * A step time may be state-relative: "s11+2000:>:300" = 2 s after controller reads (re)start while the
 * game state (D_80362690->state, see game_probe.c) is 11: on entry, and again after any >0.5 s gap
 * (menus poll briefly, then stop while they load). It fires only while the game is in that state. Menu loads stop polling for seconds and
 * vary run to run, so gate scripts use this form.
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

typedef struct { uint32_t at, hold, polls; int16_t state; uint16_t buttons; int8_t sx, sy; uint64_t start; } step_t;
extern int wings64_game_state(void);
static int64_t sStateEntered[64];   /* ms polling (re)started in each game state: entry or end of a load gap; -1 = not yet */
/* A press lasts its hold time AND at least AUTOPLAY_MIN_POLLS controller reads: the game stops polling
   for seconds after menu changes, and a press that falls in such a gap would otherwise be lost. */
#define AUTOPLAY_MIN_POLLS 3
static step_t steps[64];
static int nsteps = -1;
static uint64_t t0;

static void parse(void) {
    const char *p = DB_AUTOPLAY_SCRIPT;
    nsteps = 0;
    while (*p && nsteps < 64) {
        step_t s = { 0 };
        s.state = -1;
        if (*p == 's') {
            s.state = (int16_t)strtol(p + 1, (char **)&p, 10);
            if (*p == '+') p++;
        }
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
    if (nsteps < 0) {
        parse(); t0 = timer_ms_gettime64();
        for (int i = 0; i < 64; i++) sStateEntered[i] = -1;
    }
    now = timer_ms_gettime64() - t0;

    {
        static uint64_t last;
        static int last_st = -2;
        int st = wings64_game_state(), gap = last && now - last > 500;
        if (gap) db_log("autoplay: poll gap %lu ms at %lu ms\n", (unsigned long)(now - last), (unsigned long)now);
        /* menus poll briefly on entry, then stop for seconds while they load: re-arm after a gap */
        if (st >= 0 && st < 64 && (st != last_st || gap)) sStateEntered[st] = (int64_t)now;
        last_st = st;
        last = now;
    }
    for (int i = 0; i < nsteps; i++) {
        uint64_t at = steps[i].at;
        if (steps[i].polls) at = steps[i].start;               /* started: its window is fixed */
        else if (steps[i].state >= 0) {
            if (steps[i].state >= 64 || sStateEntered[steps[i].state] < 0) continue;
            if (wings64_game_state() != steps[i].state) continue;   /* only fire while in that state */
            at += (uint64_t)sStateEntered[steps[i].state];
        }
        if (now >= at && (now < at + steps[i].hold || steps[i].polls < AUTOPLAY_MIN_POLLS)) {
            if (!steps[i].polls) steps[i].start = now;
            steps[i].polls++;
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
