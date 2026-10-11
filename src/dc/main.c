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

#include <kos.h>
#include <malloc.h>
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
#define ROW_MEM   14
#define ROW_RESULT 15
#define ROW_FRAME 17

static void tag_name(uint32_t tag, char out[5]) {
    out[0] = (char)(tag >> 24); out[1] = (char)(tag >> 16); out[2] = (char)(tag >> 8); out[3] = (char)tag; out[4] = 0;
}

static void load_progress(uint32_t done, uint32_t total) {
    screen_line(ROW_LOAD, SCREEN_YELLOW, "loading wings64.fs  %3lu%%  (%lu KB)",
                (unsigned long)((uint64_t)done * 100u / total), (unsigned long)(done >> 10));
    screen_frame();
}

/* XMTRX round trip: translation applied to a point. Confirms the SH4 backend links and runs. */
static int xmtrx_selftest(void) {
    shz_vec4_t p;
    shz_xmtrx_init_translation(1.0f, 2.0f, 3.0f);
    p = shz_xmtrx_transform_vec4(shz_vec4_init(1.0f, 1.0f, 1.0f, 1.0f));
    return p.x == 2.0f && p.y == 3.0f && p.z == 4.0f && p.w == 1.0f;
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
    screen_line(ROW_TITLE, SCREEN_CYAN, "wings64-dc  M%d boot check", WINGS64_MILESTONE);
    screen_line(ROW_BUILD, SCREEN_GREY, "build %s", wings64_build_id);
    math_ok = xmtrx_selftest();
    screen_line(ROW_SHZ, math_ok ? SCREEN_GREY : SCREEN_RED, "sh4zam %d.%d.%d SH4 xmtrx %s",
                SHZ_VERSION_MAJOR, SHZ_VERSION_MINOR, SHZ_VERSION_PATCH, math_ok ? "ok" : "FAIL");
    ok &= math_ok;
    screen_line(ROW_DECOMP, SCREEN_GREY, "decomp 1a8558f linked, bootproc @ %p", (void *)wings64_decomp_entry);
    screen_frame();

    root = assetroot_probe();
    if (!root) {
        screen_line(ROW_ROOT, SCREEN_RED, "no disc.id on /cd or /rd");
        ok = 0;
        goto done;
    }
    screen_line(ROW_ROOT, SCREEN_WHITE, "root %s  disc %s", root, assetroot_disc_id());
    if (romfs_meta_read(root, &meta) != 0)
        screen_line(ROW_CRC, SCREEN_YELLOW, "fsmeta.txt missing: no CRC check");

    t0 = timer_ms_gettime64();
    if (romfs_load(root, load_progress) != 0) {
        screen_line(ROW_LOAD, SCREEN_RED, "wings64.fs load FAILED");
        ok = 0;
        goto done;
    }
    t_load = timer_ms_gettime64() - t0;
    screen_line(ROW_LOAD, SCREEN_WHITE, "wings64.fs %lu bytes in %lu ms (%lu KB/s)",
                (unsigned long)romfs_size, (unsigned long)t_load,
                (unsigned long)(t_load ? (romfs_size / 1024u) * 1000u / (uint32_t)t_load : 0));
    db_log("romfs: %lu bytes in %lu ms\n", (unsigned long)romfs_size, (unsigned long)t_load);

    crc = romfs_crc32();
    if (meta.valid) {
        int crc_ok = crc == meta.crc32 && romfs_size == meta.size;
        screen_line(ROW_CRC, crc_ok ? SCREEN_GREEN : SCREEN_RED, "crc32 %08lX  manifest %08lX  %s",
                    (unsigned long)crc, (unsigned long)meta.crc32, crc_ok ? "match" : "MISMATCH");
        ok &= crc_ok;
    }
    db_log("romfs: crc32 %08lX\n", (unsigned long)crc);

    {
        int walk_ok = romfs_count(&counts) == 0 && counts.walk_end == ROMFS_FILESYS_END;
        int count_ok = !meta.valid || (counts.uv_files == meta.uv_files && counts.index_entries == meta.index_entries);
        screen_line(ROW_INDEX, count_ok ? SCREEN_GREEN : SCREEN_RED, "UV files %lu  (index %lu, manifest %lu)",
                    (unsigned long)counts.uv_files, (unsigned long)counts.index_entries,
                    (unsigned long)(meta.valid ? meta.uv_files : 0));
        screen_line(ROW_FORM, walk_ok ? SCREEN_GREEN : SCREEN_RED, "FORM headers ok %lu/%lu  walk end %06lX",
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
            screen_line(ROW_TAGS + r, SCREEN_GREY, "%s", line);
        }
    }

done:
    {
        /* Free RAM = top of 16 MB minus the heap break (mallinfo().fordblks is only arena slack). */
        extern char end[];
        uint32_t brk = (uint32_t)mm_sbrk(0);
        screen_line(ROW_MEM, SCREEN_GREY, "RAM image %lu KB  heap %lu KB  free %lu KB",
                    (unsigned long)(((uint32_t)end - 0x8C000000u) >> 10),
                    (unsigned long)((brk - (uint32_t)end) >> 10), (unsigned long)((0x8D000000u - brk) >> 10));
    }
    ok &= wings64_decomp_entry != NULL;

    /* M2: hand over to the game. bootproc creates the kernel thread and returns. */
    if (ok && WINGS64_MILESTONE >= 2) {
        wings64_rdram_init();
        db_log("boot: starting decomp bootproc\n");
        bootproc(NULL);
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
                if (st == 5) seen_flight = 1;                                   /* test update = in flight */
                if (st == 6 && seen_flight) db_log("WINGS64_M3_OK\n");         /* M3: flight -> results */
                last = st;
            }
#if DB_GFX_TRACE
            if (ms == 20000) { extern void wings64_dump_model_scales(void); wings64_dump_model_scales(); }
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
    screen_line(ROW_RESULT, ok ? SCREEN_GREEN : SCREEN_RED, "M%d RESULT: %s", WINGS64_MILESTONE, ok ? "PASS" : "FAIL");
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
