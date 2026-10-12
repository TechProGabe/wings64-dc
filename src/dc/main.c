/* main.c — wings64-dc entry point.
 *
 * Boot check screen for M0-M1 (M2 replaces it with the game's own boot).
 * M1 adds: the patched decomp is linked (bootproc reachable, not yet called).
 * M0: bring up KOS video + PVR, find the disc, load disc/wings64.fs into RAM (ROM-in-RAM, PLAN D2),
 * then validate it the way the game will: CRC-32 against the manifest cut by tools/rom/slice_fs.py,
 * and a re-implementation of uvMemInitBlockHdr that decompresses the MIO0 TABL index and walks all
 * UV files checking FORM headers (proves byte-wise big-endian reads on the SH-4).
 * Results go to the screen (the hardware evidence channel; CD-R only, no serial cable), to dbglog,
 * and with DB_LOG_SD to /sd/wings64/.
 */
#include "db.h"
#include "dcpvr.h"
#include "screen.h"
#include "assetroot.h"
#include "rom/romfs.h"
#include "milestone.h"
#include "gfx/gfx_pvr.h"
#include "audio/aica_drv.h"
#include "audio/aica_synth.h"
#include "prof.h"

#include <kos.h>
#include <malloc.h>
#include <string.h>
#include <sh4zam/shz_sh4zam.h>

#if defined(__SH4__) && SHZ_BACKEND != SHZ_SH4
#error "sh4zam fell back to the SW backend: the build is not -m4-single-only (docs/sh4zam-mandate.md N.1.1)"
#endif

KOS_INIT_FLAGS(INIT_DEFAULT);

/* The decomp's boot entry (src/kernel/system.c). Referenced, not called, until M2: keeping it reachable
   makes the link resolve the whole game (kernel + app + libultra audio/sp) against the port. */
extern void bootproc(void *arg);
void wings64_rdram_init(void);
int wings64_game_state(void);
static const char *const kStateNames[] = { "title", "1", "test details", "pilot select", "test setup", "test update",
    "results", "options", "demo pilot", "demo test setup", "file menu", "vehicle & class select", "test overview",
    "results cb", "congratulations", "credits" };
unsigned wings64_gfx_tasks(void), wings64_aud_tasks(void), wings64_vi_retraces(void), wings64_pi_zero_reads(void);
void (*volatile wings64_decomp_entry)(void *) = bootproc;

#define ROW_TITLE 0
#define ROW_BUILD 1
#define ROW_SHZ   2
#define ROW_DECOMP 3
#define ROW_ROOT  4
#define ROW_LOAD  5
#define ROW_CRC   6
#define ROW_INDEX 7
#define ROW_FORM  8
#define ROW_TAGS  9   /* 9..12 */
#define ROW_AUDIO 13
#define ROW_MEM   14
#define ROW_RESULT 15
#define ROW_FRAME 17

/* DB_BOOT_VERBOSE=1: the boot check's full text (build id, sh4zam, decomp, every table count and CRC row).
   0 (default, the console look): a black screen with "Loading" and a progress bar; only failures print
   text, in red. The serial log (db_log) carries every line either way. */
#ifndef DB_BOOT_VERBOSE
#define DB_BOOT_VERBOSE 0
#endif
#define BOOT_LINE(row, color, ...) \
    do { if (DB_BOOT_VERBOSE || (color) == SCREEN_RED) screen_line(row, color, __VA_ARGS__); } while (0)

#ifndef WINGS64_AUDIO
#define WINGS64_AUDIO 1
#endif
#ifndef DB_SOAK
#define DB_SOAK 0     /* 1: no-input attract-mode soak; WINGS64_M4_SOAK_OK after DB_SOAK_S seconds */
#endif
#ifndef DB_SOAK_S
#define DB_SOAK_S 600
#endif

/* M4 audio evidence (docs/audio-design.md section 7): the gate cannot hear, so it reads counters. */
typedef struct {
    aud_stats_t at_flight;          /* counters when flight starts */
    int hw_on_menu, hw_on_flight;   /* a snapshot saw AICA channels keyed on */
    unsigned last_frames, stall_windows, minutes_quiet;
    unsigned last_starts;
    unsigned long long last_ai;
} aud_gate_t;
static aud_gate_t sAud;

#if WINGS64_AUDIO
static void audio_tick(unsigned ms, int st, int seen_flight) {
    aud_stats_t a;
    unsigned exp, hw, bad;
    if (!WINGS64_AUDIO || !aud_drv_ready()) return;
    aud_synth_stats(&a);
    bad = aud_synth_snapshot(&exp, &hw);
    if (hw && !seen_flight) sAud.hw_on_menu = 1;
    if (hw && st == 5) sAud.hw_on_flight = 1;
    if (ms % 5000 == 0) {
        unsigned df = a.frames - sAud.last_frames;
        if (ms > 10000 && df < 270) sAud.stall_windows++;          /* < 90% of 300 audio frames in 5 s */
        db_log("aud: t %u s frames %u (+%u) starts sfx %u mus %u steals %u clamps %u misses %u on %u/%u hw %u "
               "mismatch %u peak %u underruns %u rate %lu Hz cpu avg %u max %u us\n", ms / 1000, a.frames, df, a.starts[0], a.starts[1],
               a.steals, a.clamps, a.misses, a.active, exp, hw, bad, a.peak_active, a.ai_underruns,
               (unsigned long)((a.ai_samples - sAud.last_ai) / 5), a.us_frames ? a.us_sum / a.us_frames : 0, a.us_max);
        aud_synth_cpu_reset();
        sAud.last_frames = a.frames;
        sAud.last_ai = a.ai_samples;
    }
    if (ms % 60000 == 0 && ms) {
        unsigned st_all = a.starts[0] + a.starts[1];
        if (st_all == sAud.last_starts) sAud.minutes_quiet++;
        sAud.last_starts = st_all;
    }
}

/* Per-thread CPU split over the last window (KOS accounts time at each context switch, so this is not
   biased like PC sampling): N64 thread ids 0 render, 1 kernel (idle spin), 3 audio, 4 scheduler (runs the
   display-list interpreter), 6 app (game logic), plus the main monitor thread. */
static void thread_cpu_log(void) {
    extern int wings64_thread_info(int i, int *id, void **kthd);
    extern unsigned long long kb_thread_cpu_ns(void *kthd), kb_total_cpu_ns(void);
    static unsigned long long last[16], last_total, last_main;
    unsigned long long total = kb_total_cpu_ns(), win = total - last_total, c;
    char line[200];
    int n = 0, id, i;
    void *k;
    if (!win) return;
    for (i = 0; i < 15 && wings64_thread_info(i, &id, &k); i++) {
        c = kb_thread_cpu_ns(k);
        n += snprintf(line + n, sizeof line - (size_t)n, " t%d %llu%%", id, (c - last[i]) * 100 / win);
        last[i] = c;
    }
    c = kb_thread_cpu_ns(thd_get_current());
    n += snprintf(line + n, sizeof line - (size_t)n, " main %llu%%", (c - last_main) * 100 / win);
    last_main = c;
    last_total = total;
    db_log("cpu:%s over %llu ms\n", line, win / 1000000);
}

/* M4 exit, judged when the M3 flight reaches results: music and SFX started in the menus and in flight,
   AICA channels seen keyed on in both, every wave found, the audio thread never stalled. */
static int audio_gate(void) {
    aud_stats_t a;
    unsigned mus_menu, mus_flight, sfx_all, underrun_pct;
    aud_synth_stats(&a);
    mus_menu = sAud.at_flight.starts[1];
    mus_flight = a.starts[1] - sAud.at_flight.starts[1];
    sfx_all = a.starts[0];
    underrun_pct = a.frames ? a.ai_underruns * 100u / a.frames : 100u;
    db_log("aud: gate music menu %u flight %u, sfx %u, hw on menu %d flight %d, misses %u, stalls %u, "
           "underruns %u%%\n", mus_menu, mus_flight, sfx_all, sAud.hw_on_menu, sAud.hw_on_flight, a.misses,
           sAud.stall_windows, underrun_pct);
    return aud_drv_ready() && mus_menu >= 1 && mus_flight >= 1 && sfx_all >= 20 && sAud.hw_on_menu &&
           sAud.hw_on_flight && a.misses == 0 && sAud.stall_windows == 0 && underrun_pct <= 2;
}
#else
static void audio_tick(unsigned ms, int st, int seen_flight) { (void)ms; (void)st; (void)seen_flight; }
static int audio_gate(void) { db_log("aud: WINGS64_AUDIO=0 (silent build)\n"); return 0; }
#define aud_synth_stats(out) memset((out), 0, sizeof *(out))
#endif

static void tag_name(uint32_t tag, char out[5]) {
    out[0] = (char)(tag >> 24); out[1] = (char)(tag >> 16); out[2] = (char)(tag >> 8); out[3] = (char)tag; out[4] = 0;
}

static void load_progress(uint32_t done, uint32_t total) {
    if (DB_BOOT_VERBOSE)
        screen_line(ROW_LOAD, SCREEN_YELLOW, "loading wings64.fs  %3lu%%  (%lu KB)",
                    (unsigned long)((uint64_t)done * 100u / total), (unsigned long)(done >> 10));
    else
        screen_bar((int)((uint64_t)done * 100u / total));
    screen_frame();
}

/* XMTRX round trip: translation applied to a point. Confirms the SH4 backend links and runs. */
static int xmtrx_selftest(void) {
    shz_vec4_t p;
    shz_xmtrx_init_translation(1.0f, 2.0f, 3.0f);
    p = shz_xmtrx_transform_vec4(shz_vec4_init(1.0f, 1.0f, 1.0f, 1.0f));
    return p.x == 2.0f && p.y == 3.0f && p.z == 4.0f && p.w == 1.0f;
}

/* Stall watchdog (DB builds): no display list for STALL_MS while the game runs is a hang (hardware run 2 froze
   at a test overview with every thread alive). Log each KOS thread's state, PC, PR, wait reason and the
   text addresses found on its stack (decode with addr2line on the disc's ELF). Menu loads can pause drawing
   for a few seconds, so the first report comes at 3 s, a second one plus a profiler dump at 10 s, then every
   30 s while it lasts. */
#define STALL_MS 3000
typedef struct { int tid, prio, state; uint32_t pc, pr, sp; const char *wait; void *k; char label[16]; } stall_thd_t;
static stall_thd_t sStallThd[24];
static int sStallN;
static int stall_collect(kthread_t *t, void *d) {
    stall_thd_t *s;
    (void)d;
    if (sStallN == 24) return 0;
    s = &sStallThd[sStallN++];
    s->tid = (int)t->tid; s->prio = (int)t->prio; s->state = (int)t->state;
    s->pc = t->context.pc; s->pr = t->context.pr; s->sp = t->context.r[15];
    s->wait = t->state == STATE_WAIT ? t->wait_msg : NULL;
    s->k = t;
    snprintf(s->label, sizeof s->label, "%s", t == thd_get_current() ? "main(me)" : t->label);
    return 0;
}
static void stall_report(unsigned ms, int st, unsigned stalled_ms, int dump) {
    const uint32_t lo = 0x8c010000u, hi = (uint32_t)&_etext;
    int old = irq_disable();
    sStallN = 0;
    thd_each(stall_collect, NULL);
    irq_restore(old);
    db_log("stall: no display list for %u ms (state %d, t %u ms, gfx %u, vi %u)\n", stalled_ms, st, ms,
           wings64_gfx_tasks(), wings64_vi_retraces());
    for (int i = 0; i < sStallN; i++) {
        stall_thd_t *s = &sStallThd[i];
        char bt[120];
        int n = 0, k = 0;
        bt[0] = 0;
        {   /* N64 thread id (0 render, 1 kernel, 3 audio, 4 scheduler, 6 app) in place of the "n64" label */
            extern int wings64_thread_info(int i, int *id, void **kthd);
            int id;
            void *k;
            for (int j = 0; wings64_thread_info(j, &id, &k); j++)
                if (k == s->k) snprintf(s->label, sizeof s->label, "n64-t%d", id);
        }
        /* poor man's backtrace: words on the stack that point into .text (return addresses, mostly) */
        if (s->state != STATE_RUNNING && s->sp >= 0x8c000000u && s->sp < 0x8d000000u)
            for (const uint32_t *p = (const uint32_t *)(s->sp & ~3u); k < 96 && n < (int)sizeof bt - 10; p++, k++)
                if (*p >= lo && *p < hi && (*p & 1) == 0) n += snprintf(bt + n, sizeof bt - (size_t)n, " %08lx", (unsigned long)*p);
        db_log("stall: thd %d %-10s prio %d state %d pc %08lx pr %08lx wait %s |%s\n", s->tid, s->label, s->prio,
               s->state, (unsigned long)s->pc, (unsigned long)s->pr, s->wait ? s->wait : "-", bt);
    }
    if (dump) prof_dump_now("stall");
}
static void stall_watch(unsigned ms, int st) {
    static unsigned last_gfx, last_change_ms, reports;
    unsigned g = wings64_gfx_tasks(), stalled = ms - last_change_ms;
    if (g != last_gfx || st < 0) { last_gfx = g; last_change_ms = ms; reports = 0; return; }
    if (stalled < (reports == 0 ? STALL_MS : reports == 1 ? 10000u : 10000u + 30000u * (reports - 1))) return;
    stall_report(ms, st, stalled, reports == 1);
    reports++;
}

int main(int argc, char **argv) {
    const char *root;
    romfs_meta_t meta;
    romfs_counts_t counts;
    uint64_t t0, t_load;
    uint32_t crc = 0, frame = 0;
    int ok = 1, math_ok;
    (void)argc; (void)argv;

    vid_set_mode(DM_640x480, PM_RGB565);   /* 480i on composite/S-video, 480p on VGA; never 50 Hz */
    dc_pvr_init();
    gpvr_init();
    db_init();
    screen_init();

    db_log("wings64-dc %s\n", wings64_build_id);
    if (!DB_BOOT_VERBOSE) screen_loading();
    BOOT_LINE(ROW_TITLE, SCREEN_CYAN, "wings64-dc  M%d boot check", WINGS64_MILESTONE);
    BOOT_LINE(ROW_BUILD, SCREEN_GREY, "build %s", wings64_build_id);
    math_ok = xmtrx_selftest();
#ifdef DB_FSQRT_TEST
    { extern void wings64_fsqrt_test(void); wings64_fsqrt_test(); }
#endif
    BOOT_LINE(ROW_SHZ, math_ok ? SCREEN_GREY : SCREEN_RED, "sh4zam %d.%d.%d SH4 xmtrx %s",
              SHZ_VERSION_MAJOR, SHZ_VERSION_MINOR, SHZ_VERSION_PATCH, math_ok ? "ok" : "FAIL");
    ok &= math_ok;
    BOOT_LINE(ROW_DECOMP, SCREEN_GREY, "decomp 1a8558f linked, bootproc @ %p", (void *)wings64_decomp_entry);
    screen_frame();

    root = assetroot_probe();
    if (!root) {
        screen_line(ROW_ROOT, SCREEN_RED, "no disc.id on /cd or /rd");
        ok = 0;
        goto done;
    }
    BOOT_LINE(ROW_ROOT, SCREEN_WHITE, "root %s  disc %s", root, assetroot_disc_id());
    if (romfs_meta_read(root, &meta) != 0)
        BOOT_LINE(ROW_CRC, SCREEN_YELLOW, "fsmeta.txt missing: no CRC check");

    t0 = timer_ms_gettime64();
    if (romfs_load(root, load_progress) != 0) {
        screen_line(ROW_LOAD, SCREEN_RED, "wings64.fs load FAILED");
        ok = 0;
        goto done;
    }
    t_load = timer_ms_gettime64() - t0;
    BOOT_LINE(ROW_LOAD, SCREEN_WHITE, "wings64.fs %lu bytes in %lu ms (%lu KB/s)",
                (unsigned long)romfs_size, (unsigned long)t_load,
                (unsigned long)(t_load ? (romfs_size / 1024u) * 1000u / (uint32_t)t_load : 0));
    db_log("romfs: %lu bytes in %lu ms\n", (unsigned long)romfs_size, (unsigned long)t_load);

#if WINGS64_AUDIO
    {
        uint64_t ta = timer_ms_gettime64();
        int aok = aud_drv_init(root) == 0;
        BOOT_LINE(ROW_AUDIO, aok ? SCREEN_GREY : SCREEN_RED, "audio pool %lu KB, %u waves, %lu ms%s",
                    (unsigned long)(aud_drv_pool_bytes() >> 10), aud_drv_wave_count(),
                    (unsigned long)(timer_ms_gettime64() - ta), aok ? "" : "  FAILED (silent)");
    }
#endif
#ifndef WINGS64_CD_SPINDOWN
#define WINGS64_CD_SPINDOWN 1
#endif
#if WINGS64_CD_SPINDOWN
    /* Everything the game reads is resident now (romfs in RAM, the audio pool in AICA RAM): stop the disc so
       the drive does not spin flat out for the whole session (hardware run 6: loud, strained drive). A later
       read spins it up again. Kill switch -DWINGS64_CD_SPINDOWN=0. */
    if (!strcmp(root, "/cd")) db_log("cd: spin down %s\n", cdrom_exec_cmd(CMD_STOP, NULL) == ERR_OK ? "ok" : "failed");
#endif
    crc = romfs_crc32();
    if (meta.valid) {
        int crc_ok = crc == meta.crc32 && romfs_size == meta.size;
        BOOT_LINE(ROW_CRC, crc_ok ? SCREEN_GREEN : SCREEN_RED, "crc32 %08lX  manifest %08lX  %s",
                    (unsigned long)crc, (unsigned long)meta.crc32, crc_ok ? "match" : "MISMATCH");
        ok &= crc_ok;
    }
    db_log("romfs: crc32 %08lX\n", (unsigned long)crc);

    {
        int walk_ok = romfs_count(&counts) == 0 && counts.walk_end == ROMFS_FILESYS_END;
        int count_ok = !meta.valid || (counts.uv_files == meta.uv_files && counts.index_entries == meta.index_entries);
        BOOT_LINE(ROW_INDEX, count_ok ? SCREEN_GREEN : SCREEN_RED, "UV files %lu  (index %lu, manifest %lu)",
                    (unsigned long)counts.uv_files, (unsigned long)counts.index_entries,
                    (unsigned long)(meta.valid ? meta.uv_files : 0));
        BOOT_LINE(ROW_FORM, walk_ok ? SCREEN_GREEN : SCREEN_RED, "FORM headers ok %lu/%lu  walk end %06lX",
                    (unsigned long)counts.form_ok, (unsigned long)counts.uv_files, (unsigned long)counts.walk_end);
        ok &= walk_ok && count_ok;
        db_log("romfs: uv_files %lu form_ok %lu walk_end %06lX\n", (unsigned long)counts.uv_files,
               (unsigned long)counts.form_ok, (unsigned long)counts.walk_end);
        /* Tag histogram, five per row, four rows. */
        for (int r = 0; r < 4; r++) {
            char line[64] = "";
            int n = 0;
            for (uint32_t i = (uint32_t)r * 5; i < counts.ntags && i < (uint32_t)r * 5 + 5; i++) {
                char nm[5];
                tag_name(counts.tag[i], nm);
                n += snprintf(line + n, sizeof line - (size_t)n, "%s %lu  ", nm, (unsigned long)counts.count[i]);
            }
            BOOT_LINE(ROW_TAGS + r, SCREEN_GREY, "%s", line);
        }
    }

done:
    {
        /* Free RAM = top of 16 MB minus the heap break (mallinfo().fordblks is only arena slack). */
        extern char end[];
        uint32_t brk = (uint32_t)mm_sbrk(0);
        BOOT_LINE(ROW_MEM, SCREEN_GREY, "RAM image %lu KB  heap %lu KB  free %lu KB",
                    (unsigned long)(((uint32_t)end - 0x8C000000u) >> 10),
                    (unsigned long)((brk - (uint32_t)end) >> 10), (unsigned long)((0x8D000000u - brk) >> 10));
    }
    ok &= wings64_decomp_entry != NULL;

    /* M2: hand over to the game. bootproc creates the kernel thread and returns. */
    if (ok && WINGS64_MILESTONE >= 2) {
        wings64_rdram_init();
        db_log("boot: starting decomp bootproc\n");
        if (!DB_BOOT_VERBOSE) { screen_bar(100); screen_frame(); } /* the last frame the console shows before the game draws */
        bootproc(NULL);
        prof_init();     /* DB_PROF: chained on the scheduler tick that bootproc's threads now run on */
        /* The game's scheduler thread owns the PVR from here (gfx_f3d.c); main only reports.
           M2 gate: the state machine must pass title -> file menu -> vehicle & class select. */
        int last = -2, seen_title = 0, seen_file = 0, seen_flight = 0;
        for (unsigned ms = 0;; ms += 250) {
            int st = wings64_game_state();
            thd_sleep(250);
            if (st != last) {
                db_log("state: %d (%s) at %u ms, gfx %u\n", st, st >= 0 && st < 16 ? kStateNames[st] : "?", ms,
                       wings64_gfx_tasks());
                if (st == 0) seen_title = 1;
#if DB_GFX_TRACE
                { extern void wings64_dump_model_scales(void); if (st == 0 && ms > 0) wings64_dump_model_scales(); }
#endif
                if (st == 10 && seen_title) seen_file = 1;
                if (st == 11 && seen_file) db_log("WINGS64_M2_OK\n");
                if (st == 5 && !seen_flight) aud_synth_stats(&sAud.at_flight);
                if (st == 5) seen_flight = 1;                                   /* test update = in flight */
                if (st == 6 && seen_flight) {                                   /* M3: flight -> results */
                    db_log("WINGS64_M3_OK\n");
                    if (WINGS64_MILESTONE >= 4 && !DB_SOAK) db_log(audio_gate() ? "WINGS64_M4_OK\n" : "WINGS64_M4_FAIL\n");
                }
                last = st;
            }
#if DB_GFX_TRACE
            if (ms == 20000) { extern void wings64_dump_model_scales(void); wings64_dump_model_scales(); }
#endif
            prof_poll(ms, st);
            if (DB_ENABLE) stall_watch(ms, st);
            if (ms % 1000 == 0) audio_tick(ms, st, seen_flight);
            if (ms % 5000 == 0) thread_cpu_log();
#if DB_SOAK && WINGS64_AUDIO
            /* wall-clock time: the 250 ms loop counter runs slow against it */
            static uint64_t soak_t0;
            static int soak_done;
            if (!soak_t0) soak_t0 = timer_ms_gettime64();
            if (!soak_done && timer_ms_gettime64() - soak_t0 >= DB_SOAK_S * 1000ull) {
                soak_done = 1;
                aud_stats_t a;
                aud_synth_stats(&a);
                int sok = a.misses == 0 && sAud.stall_windows == 0 && sAud.minutes_quiet == 0 &&
                          a.starts[0] > 0 && a.starts[1] > 0 && (a.frames ? a.ai_underruns * 100u / a.frames : 100u) <= 2;
                db_log("aud: soak %u s: frames %u starts sfx %u mus %u misses %u stalls %u quiet minutes %u underruns %u\n",
                       DB_SOAK_S, a.frames, a.starts[0], a.starts[1], a.misses, sAud.stall_windows, sAud.minutes_quiet,
                       a.ai_underruns);
                db_log(sok ? "WINGS64_M4_SOAK_OK\n" : "WINGS64_M4_SOAK_FAIL\n");
            }
#endif
            if (ms % 1000 == 0) {
                db_log("game: t %u s gfx %u aud %u vi %u pi0 %u\n", ms / 1000, wings64_gfx_tasks(), wings64_aud_tasks(),
                       wings64_vi_retraces(), wings64_pi_zero_reads());
                if (ms == 5000) db_log("WINGS64_M2_GAMELOOP\n");
#ifdef DB_CRASH_TEST
                if (ms == 10000) { db_log("db: DB_CRASH_TEST illegal instruction\n"); __asm__ volatile(".word 0xfffd"); }
#endif
                db_sd_poll(ms);
            }
        }
    }
    BOOT_LINE(ROW_RESULT, ok ? SCREEN_GREEN : SCREEN_RED, "M%d RESULT: %s", WINGS64_MILESTONE, ok ? "PASS" : "FAIL");
    /* M0/M1 gates end here; from M2 the marker comes from the game's own state machine. */
    if (WINGS64_MILESTONE < 2 || !ok) db_log("WINGS64_M%d_%s\n", WINGS64_MILESTONE, ok ? "OK" : "FAIL");
    db_sd_flush("exit");

    for (;;) {
        /* Seconds counter: proves video + PVR keep running; text changes force a 1 MB upload. */
        if (frame % 60 == 0) {
            screen_line(ROW_FRAME, SCREEN_GREY, "alive %lu s", (unsigned long)(frame / 60));
            if (WINGS64_MILESTONE >= 2) {
                static unsigned last_gfx;
                unsigned g = wings64_gfx_tasks();
                screen_line(ROW_FRAME + 1, SCREEN_YELLOW, "game: gfx %u (+%u/s) aud %u vi %u pi0 %u", g, g - last_gfx,
                            wings64_aud_tasks(), wings64_vi_retraces(), wings64_pi_zero_reads());
                db_log("game: gfx %u aud %u vi %u pi0 %u\n", g, wings64_aud_tasks(), wings64_vi_retraces(),
                       wings64_pi_zero_reads());
                if (last_gfx < 120 && g >= 120) db_log("WINGS64_M2_GAMELOOP\n");
                last_gfx = g;
                db_sd_poll(frame * 1000u / 60u);
            }
        }
        frame++;
        screen_frame();
    }
    return 0;
}
