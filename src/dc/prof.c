/* prof.c — PC-sampling profiler for hardware runs (DB_PROF). The console has no debugger link, so this is
 * how a burned disc says where the SH-4 time goes.
 *
 * KOS's thread scheduler takes the primary timer interrupt at thd_set_hz (1000 Hz, os_shim.c); our
 * callback is chained in front of it and counts the interrupted PC into 16-byte bins over .text. Idle time
 * lands in the KOS idle thread's loop, so it shows up as a function like any other.
 * Dump: hold L + R + Start (controller 1) for half a second, or DB_PROF_AUTO_S seconds after the game
 * starts; the histogram goes to WINGS64/PROFnnn.BIN on the SD card and the counts restart, so each dump is
 * one window. tools/prof/decode_prof.py maps the bins to functions with the ELF the disc was built from.
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
    if (rc == 0) {
        sDumps++;
        memset(sBins, 0, sNumBins * 4);
        sSamples = sOutside = 0;
        sWindowStart = timer_ms_gettime64();
    }
    sPaused = 0;
}

/* Main monitor thread, every 250 ms. */
void prof_poll(unsigned ms) {
    static unsigned held;
    if (!sBins) return;
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
