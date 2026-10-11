/* prof.c — PC-sampling profiler for hardware runs (DB_PROF). The console has no debugger link, so this is
 * how a burned disc says where the SH-4 time goes.
 *
 * KOS's thread scheduler takes the primary timer interrupt at thd_set_hz (1000 Hz, os_shim.c); our
 * callback is chained in front of it and counts the interrupted PC into 16-byte bins over .text. Idle time
 * lands in the KOS idle thread's loop, so it shows up as a function like any other.
 * Dump: on every game state change (so each dump is one screen: title, menu, flight...), when L + R + Start
 * (controller 1) is held for half a second, or DB_PROF_AUTO_S seconds in; the histogram goes to
 * WINGS64/PROFnnn.BIN on the SD card and the counts restart, so each dump is one window. Without a card
 * (Flycast) the DB_PROF_SERIAL hottest bins are logged instead ("prof: bin <pc> <count>"). tools/prof/decode_prof.py maps the bins to functions with the ELF the disc was built from.
 *
 * File: header (8 x u32: magic "WPRF", version 1, text start, bin shift, bin count, samples, samples
 * outside .text, window ms) then bin count x u32.
 */
#include "prof.h"
#include "db.h"

#include <kos.h>
#include <arch/timer.h>
#include <arch/irq.h>
#include <string.h>

#if DB_PROF

#define BIN_SHIFT 4
#ifndef DB_PROF_SERIAL
#define DB_PROF_SERIAL 48     /* bins logged when the card write fails (Flycast) */
#endif
#define PROF_MAGIC 0x46525057u   /* "WPRF" */

/* .text bounds: KOS arch.h declares the linker's __executable_start / __etext as _executable_start / _etext */

static uint32_t *sBins;
static uint32_t sNumBins, sLo, sHi;
static volatile uint32_t sSamples, sOutside;
static volatile int sPaused;
static timer_primary_callback_t sNext;
static uint64_t sWindowStart;
static unsigned sDumps;

static void prof_tick(irq_context_t *cxt) {
    if (!sPaused) {
        uint32_t pc = CONTEXT_PC(*cxt);
        if (pc >= sLo && pc < sHi) sBins[(pc - sLo) >> BIN_SHIFT]++;
        else sOutside++;
        sSamples++;
    }
    if (sNext) sNext(cxt);
}

void prof_init(void) {
    sLo = (uint32_t)&_executable_start;
    sHi = (uint32_t)&_etext;
    sNumBins = ((sHi - sLo) >> BIN_SHIFT) + 1;
    sBins = calloc(sNumBins, sizeof *sBins);
    if (!sBins) {
        db_log("prof: no memory for %lu bins\n", (unsigned long)sNumBins);
        return;
    }
    sWindowStart = timer_ms_gettime64();
    sNext = timer_primary_set_callback(prof_tick);
    db_log("prof: sampling %08lx..%08lx, %lu bins (%lu KB), hold L+R+Start to dump\n", (unsigned long)sLo,
           (unsigned long)sHi, (unsigned long)sNumBins, (unsigned long)(sNumBins * 4 / 1024));
}

static int chord_down(void) {
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *st;
    if (!dev) return 0;
    st = (cont_state_t *)maple_dev_status(dev);
    return st && st->ltrig > 200 && st->rtrig > 200 && (st->buttons & CONT_START);
}

static void dump(const char *why) {
    uint32_t hdr[8];
    char name[16];
    int rc;
    sPaused = 1;
    hdr[0] = PROF_MAGIC; hdr[1] = 1; hdr[2] = sLo; hdr[3] = BIN_SHIFT; hdr[4] = sNumBins;
    hdr[5] = sSamples; hdr[6] = sOutside; hdr[7] = (uint32_t)(timer_ms_gettime64() - sWindowStart);
    snprintf(name, sizeof name, "PROF%03u.BIN", sDumps);
    rc = db_sd_write_file(name, hdr, sizeof hdr, sBins, sNumBins * 4);
    db_log("prof: %s dump %s: %lu samples (%lu outside .text) over %lu ms -> %s\n", why, name,
           (unsigned long)hdr[5], (unsigned long)hdr[6], (unsigned long)hdr[7], rc == 0 ? "written" : "NOT written");
#if DB_PROF_SERIAL
    if (rc != 0 && hdr[5] > 100) {
        /* top bins by count, selection over a copy of the indices (dump time only) */
        uint32_t top[DB_PROF_SERIAL], topc[DB_PROF_SERIAL], k, n = 0;
        for (uint32_t i = 0; i < sNumBins; i++) {
            uint32_t c = sBins[i];
            if (!c) continue;
            if (n < DB_PROF_SERIAL) { top[n] = i; topc[n] = c; n++; }
            else {
                uint32_t m = 0;
                for (k = 1; k < n; k++) if (topc[k] < topc[m]) m = k;
                if (c > topc[m]) { top[m] = i; topc[m] = c; }
            }
        }
        for (k = 0; k < n; k++)
            db_log("prof: bin %08lx %lu\n", (unsigned long)(sLo + (top[k] << BIN_SHIFT)), (unsigned long)topc[k]);
        db_log("prof: end %s %lu\n", why, (unsigned long)hdr[5]);
    }
    if (rc != 0) {                              /* restart the window anyway: one window per dump */
        memset(sBins, 0, sNumBins * 4);
        sSamples = sOutside = 0;
        sWindowStart = timer_ms_gettime64();
    }
#endif
    if (rc == 0) {
        sDumps++;
        memset(sBins, 0, sNumBins * 4);
        sSamples = sOutside = 0;
        sWindowStart = timer_ms_gettime64();
    }
    sPaused = 0;
}

/* Main monitor thread, every 250 ms. st = game state (game_probe.c), -1 before the game runs. */
void prof_poll(unsigned ms, int st) {
    static unsigned held;
    static int last_st = -2;
    if (!sBins) return;
    if (st != last_st) {
        char why[24];
        snprintf(why, sizeof why, "state %d", last_st);
        if (last_st >= 0 && sSamples > 500) dump(why);    /* the window just ended belonged to last_st */
        else { memset(sBins, 0, sNumBins * 4); sSamples = sOutside = 0; sWindowStart = timer_ms_gettime64(); }
        last_st = st;
    }
    if (chord_down()) {
        if (++held == 2) dump("chord");
    } else {
        held = 0;
    }
#if DB_PROF_AUTO_S
    if (ms == DB_PROF_AUTO_S * 1000u) dump("auto");
#endif
    (void)ms;
}

unsigned prof_dumps(void) { return sDumps; }

#endif /* DB_PROF */
