/* db.c — debug log sink: KOS dbglog (serial / dcload console), the SD card log (DB_LOG_SD) and the
 * crash hook (DB_CRASH).
 *
 * SD log design (lessons from OpenCrossing-Dreamcast dc/src/dc_profdump.c, proven on a retail console;
 * the reasons are in kb/traps.md):
 *  - The serial-port SD adapter turns SCIF into bit-banged SPI, so it and the console cannot both be
 *    live. Text is buffered in RAM from boot; the card comes up DB_LOG_SD_ARM_MS after the game thread
 *    starts (not at main(): changing how much boot blocks changes the boot path), the console is muted
 *    for good once the card answers, and is re-enabled if it does not.
 *  - SCIF probe only by default: KOS's SCI SPI helpers wait for RDRF with no timeout.
 *  - Never O_APPEND on fs_fat: O_APPEND (0x08) lands inside O_MODE_MASK and every write fails with EBADF
 *    while the open succeeds. One file, opened O_TRUNC, held open, written sequentially.
 *  - fs_fat postpones block and inode writes: without fs_fat_sync the file on the card is empty or short.
 *    Each write is followed by a sync; the receipt file says how far the run got.
 */
#include "db.h"
#include "stamp.h"

#include <kos.h>
#include <arch/irq.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sh4zam/shz_sh4zam.h>

const char wings64_build_id[] = WINGS64_BUILD_ID;

#if DB_SD_BUILD
#include <dc/sd.h>
#include <fat/fs_fat.h>

/* Upper-case 8.3 names (OpenCrossing's proven GMON.1 / GPROF.TXT pattern): no long-file-name entries,
   whose handling in libkosfat changed after KOS v2.2.2. */
#define SD_DIR      "/sd/WINGS64"
#define SD_BUF_SIZE (64 * 1024)          /* x2 (double buffer); the boot log until arming must fit in one */
#define SD_LOG_CAP  (32u * 1024u * 1024u)
#define SD_MAX_ERR  8

enum { SD_OFF, SD_UP, SD_NOCARD, SD_STOPPED };
static volatile int sd_on = DB_LOG_SD;     /* runtime switch (settings menu); boot default DB_LOG_SD */

static char sd_buf[2][SD_BUF_SIZE] __attribute__((aligned(32)));
static volatile uint32_t sd_len[2];
static volatile int sd_cur;               /* buffer db_log appends to */
static volatile uint32_t sd_dropped;      /* bytes lost because the active buffer was full */
static volatile int sd_state = SD_OFF;
static file_t sd_fd = -1;
static uint32_t sd_written, sd_err, sd_last_sync_ms, sd_last_ms;
static char sd_path[64];
static kos_blockdev_t sd_dev;             /* file scope: fs_fat_mount keeps the pointer */
static mutex_t sd_mtx = MUTEX_INITIALIZER;

/* Any thread or IRQ: copy one line into the active buffer. Never blocks, never touches the card. */
static void sd_append(const char *s, uint32_t n) {
    irq_mask_t m;
    if (!sd_on || sd_state == SD_STOPPED) return;
    m = irq_disable();
    {
        int c = sd_cur;
        if (sd_len[c] + n <= SD_BUF_SIZE) {
            memcpy(sd_buf[c] + sd_len[c], s, n);
            sd_len[c] += n;
        } else {
            sd_dropped += n;
        }
    }
    irq_restore(m);
}

/* The bring-up step running or last failed, shown in the settings menu ("probe init", "fail mount"):
   on hardware the console is muted during the probe, so the screen is the only witness. Letters only
   (the menu font has no punctuation). */
static const char *volatile sd_step = "init";

static int sd_probe(void) {
    sd_init_params_t p;
    uint8_t ptype = 0;
    p.interface = DB_LOG_SD_IF == 1 ? SD_IF_SCI : SD_IF_SCIF;
    p.check_crc = true;
    sd_step = "init";
    if (sd_init_ex(&p) != 0) return 0;
    /* MBR only: a "superfloppy" card (no partition table) does not enumerate here. */
    sd_step = "part";
    if (sd_blockdev_for_partition(0, &sd_dev, &ptype) != 0) { sd_shutdown(); return 0; }
    sd_step = "fat";
    if (fs_fat_init() != 0) { sd_shutdown(); return 0; }
    sd_step = "mount";
    if (fs_fat_mount("/sd", &sd_dev, FS_FAT_MOUNT_READWRITE) != 0) { fs_fat_shutdown(); sd_shutdown(); return 0; }
    return 1;
}

static void sd_receipt(const char *why, unsigned int ms) {
    char txt[512];
    int n;
    file_t f = fs_open(SD_DIR "/RECEIPT.TXT", O_WRONLY | O_CREAT | O_TRUNC);
    if (f < 0) { sd_err++; return; }
    n = snprintf(txt, sizeof txt,
                 "wings64-dc %s\n"
                 "log        %s\n"
                 "state      %s\n"
                 "uptime     %u ms (game thread)\n"
                 "written    %lu bytes\n"
                 "dropped    %lu bytes (RAM buffer full between writes)\n"
                 "errors     %lu (short writes / failed opens)\n"
                 "iface      %s\n"
                 "Written at every sync; 'running' with an old uptime means power-off or a hang there.\n",
                 wings64_build_id, sd_path, why, ms, (unsigned long)sd_written, (unsigned long)sd_dropped,
                 (unsigned long)sd_err, DB_LOG_SD_IF == 1 ? "SCI" : "SCIF");
    if (n > 0 && fs_write(f, txt, (size_t)n) != n) sd_err++;
    fs_close(f);
}

/* Bring the card up. Called with sd_mtx held, from a thread (never an IRQ). */
static void sd_arm(void) {
    char hdr[160];
    int i, n;
    dbglog(DBG_INFO, "db: SD log arming (%s); the console goes silent if a card answers\n",
           DB_LOG_SD_IF == 1 ? "SCI" : "SCIF");
    dbgio_flush();
    dbgio_disable();                       /* nothing may write SCIF while it is in SPI mode */
    if (!sd_probe()) {
        dbgio_enable();
        sd_state = SD_NOCARD;
        dbglog(DBG_INFO, "db: no SD card (MBR FAT32 needed); console log only\n");
        return;
    }
    sd_step = "open";
    fs_mkdir(SD_DIR);
    for (i = 0; i < 1000; i++) {           /* first free LOGNNN.TXT: one file per boot */
        file_t f;
        snprintf(sd_path, sizeof sd_path, SD_DIR "/LOG%03d.TXT", i);
        f = fs_open(sd_path, O_RDONLY);
        if (f < 0) break;
        fs_close(f);
    }
    sd_fd = fs_open(sd_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (sd_fd < 0) {
        fs_fat_unmount("/sd"); fs_fat_shutdown(); sd_shutdown();
        dbgio_enable();
        sd_state = SD_NOCARD;
        dbglog(DBG_INFO, "db: SD card up but %s would not open; console log only\n", sd_path);
        return;
    }
    sd_state = SD_UP;
    n = snprintf(hdr, sizeof hdr, "== wings64-dc %s  SD log %s  (console muted from here) ==\n",
                 wings64_build_id, sd_path);
    if (n > 0 && fs_write(sd_fd, hdr, (size_t)n) == n) sd_written += (uint32_t)n; else sd_err++;
}

/* Write the filled buffer(s) and sync. Called with sd_mtx held. */
static void sd_write_pending(const char *why, unsigned int ms) {
    for (int pass = 0; pass < 2; pass++) {
        irq_mask_t m = irq_disable();
        int c = sd_cur;
        uint32_t len = sd_len[c];
        sd_cur = c ^ 1;                    /* appends go to the other (empty) buffer meanwhile */
        irq_restore(m);
        if (len) {
            ssize_t w = fs_write(sd_fd, sd_buf[c], len);
            if (w == (ssize_t)len) sd_written += len; else sd_err++;
        }
        sd_len[c] = 0;
    }
    if (sd_dropped) {
        char note[64];
        int n = snprintf(note, sizeof note, "== db: %lu bytes dropped (buffer full) ==\n", (unsigned long)sd_dropped);
        if (n > 0 && fs_write(sd_fd, note, (size_t)n) == n) sd_written += (uint32_t)n;
    }
    sd_receipt(why, ms);
    fs_fat_sync("/sd");
    if (sd_err >= SD_MAX_ERR || sd_written >= SD_LOG_CAP) {
        fs_close(sd_fd); sd_fd = -1;
        sd_receipt(sd_err >= SD_MAX_ERR ? "stopped: write errors" : "stopped: size cap", ms);
        fs_fat_sync("/sd");
        sd_state = SD_STOPPED;             /* keep the card mounted and the console muted: SPI owns SCIF */
    }
}

/* Take the card down cleanly and give SCIF back to the console. Called with sd_mtx held. */
static void sd_disarm(unsigned int ms) {
    sd_write_pending("switched off", ms);
    if (sd_fd >= 0) { fs_close(sd_fd); sd_fd = -1; }
    fs_fat_sync("/sd");
    fs_fat_unmount("/sd");
    fs_fat_shutdown();
    sd_shutdown();                         /* restores SCIF as a UART */
    dbgio_enable();
    sd_state = SD_OFF;
    dbglog(DBG_INFO, "db: SD log off (%s, %lu bytes); console back\n", sd_path, (unsigned long)sd_written);
}

void db_sd_poll(unsigned int ms) {
    sd_last_ms = ms;
    if (ms < DB_LOG_SD_ARM_MS || sd_state == SD_STOPPED) return;
    if (!sd_on && sd_state != SD_UP) return;
    mutex_lock(&sd_mtx);
    if (sd_on && sd_state == SD_OFF) sd_arm();
    if (!sd_on && sd_state == SD_UP) sd_disarm(ms);
    if (sd_state == SD_UP && ms - sd_last_sync_ms >= DB_LOG_SD_SYNC_MS) {
        sd_last_sync_ms = ms;
        sd_write_pending("running", ms);
    }
    mutex_unlock(&sd_mtx);
}

void db_sd_flush(const char *why) {
    if (!sd_on || sd_state == SD_STOPPED || sd_state == SD_NOCARD || irq_inside_int()) return;
    /* timed: a crash inside the SD path itself must not deadlock the report */
    if (mutex_lock_timed(&sd_mtx, 3000) != 0) return;
    if (sd_state == SD_OFF) sd_arm();
    if (sd_state == SD_UP) sd_write_pending(why, sd_last_ms);
    mutex_unlock(&sd_mtx);
}

/* Whole file next to the log (profiler dumps). Main thread; the card must be up. 0 = written. */
int db_sd_write_file(const char *name, const void *a, unsigned alen, const void *b, unsigned blen) {
    char path[64];
    file_t f;
    int ok;
    if (sd_state != SD_UP || irq_inside_int()) return -1;
    if (mutex_lock_timed(&sd_mtx, 3000) != 0) return -1;
    snprintf(path, sizeof path, SD_DIR "/%s", name);
    f = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    ok = f >= 0 && fs_write(f, a, alen) == (ssize_t)alen && (!blen || fs_write(f, b, blen) == (ssize_t)blen);
    if (f >= 0) fs_close(f);
    fs_fat_sync("/sd");
    if (!ok) sd_err++;
    mutex_unlock(&sd_mtx);
    return ok ? 0 : -1;
}

int db_sd_available(void) { return 1; }
int db_sd_enabled(void) { return sd_on; }
void db_sd_set_enabled(int on) {
    /* Takes effect at the next db_sd_poll (main monitor thread). Re-enabling after "no card" probes again. */
    if (on && !sd_on && sd_state == SD_NOCARD) sd_state = SD_OFF;
    if (on && !sd_on) { sd_len[0] = sd_len[1] = 0; sd_dropped = 0; }
    sd_on = on ? 1 : 0;
}
const char *db_sd_status(void) {
    if (!sd_on) return sd_state == SD_UP ? "turning off" : "off";
    switch (sd_state) {
    case SD_UP: return sd_path + sizeof SD_DIR;   /* "LOGNNN.TXT" */
    case SD_NOCARD: {
        static char why[24];
        snprintf(why, sizeof why, "fail %s", sd_step);
        return why;
    }
    case SD_STOPPED: return "stopped";
    }
    if (sd_last_ms < DB_LOG_SD_ARM_MS) return "starting";
    {
        static char what[24];
        snprintf(what, sizeof what, "probe %s", sd_step);
        return what;
    }
}
#else
int db_sd_write_file(const char *name, const void *a, unsigned alen, const void *b, unsigned blen) {
    (void)name; (void)a; (void)alen; (void)b; (void)blen;
    return -1;
}
void db_sd_poll(unsigned int ms) { (void)ms; }
void db_sd_flush(const char *why) { (void)why; }
int db_sd_available(void) { return 0; }
int db_sd_enabled(void) { return 0; }
void db_sd_set_enabled(int on) { (void)on; }
const char *db_sd_status(void) { return "not in this build"; }
#endif

#if DB_ENABLE
void db_log(const char *fmt, ...) {
    char line[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof line) n = sizeof line - 1;
#if DB_SD_BUILD
    if (sd_state != SD_UP) dbglog(DBG_INFO, "%s", line);   /* console is muted while the card owns SCIF */
    sd_append(line, (uint32_t)n);
#else
    dbglog(DBG_INFO, "%s", line);
#endif
}
#endif

/* The game's _uvDebugPrintf (patches/024). Each format string is logged at most 4 times (per-frame
   warnings would flood the card); the rest are counted and the count reported at the next new message. */
void wings64_uv_debug(const char *fmt, va_list ap) {
#if DB_ENABLE
    static const char *seen[64];
    static unsigned char count[64];
    static unsigned dropped;
    char line[200];
    int i, n;
    if (irq_inside_int()) return;
    for (i = 0; i < 64 && seen[i] && seen[i] != fmt; i++) {}
    if (i < 64 && !seen[i]) seen[i] = fmt;
    if (i == 64 || count[i] >= 4) { dropped++; return; }
    count[i]++;
    n = vsnprintf(line, sizeof line, fmt, ap);
    if (n < 0) return;
    if (n >= (int)sizeof line) n = sizeof line - 1;
    while (n > 0 && line[n - 1] == '\n') line[--n] = 0;
    db_log("uv: %s%s\n", line, count[i] == 4 ? " (further repeats dropped)" : "");
    if (dropped) { db_log("uv: %u repeated messages dropped so far\n", dropped); dropped = 0; }
#else
    (void)fmt; (void)ap;
#endif
}

/* ------------------------------------------------------------------ crash hook */

#if DB_CRASH
static struct {
    volatile int active;
    uint32_t code, pc, pr, sr, r[16], stack[8];
    kthread_t *thd;
} sCrash;

static const char *exc_name(uint32_t code) {
    switch (code) {
    case EXC_ILLEGAL_INSTR: return "illegal instruction";
    case EXC_SLOT_ILLEGAL_INSTR: return "illegal slot instruction";
    case EXC_DATA_ADDRESS_READ: return "address error (read)";
    case EXC_DATA_ADDRESS_WRITE: return "address error (write)";
    case EXC_USER_BREAK_PRE: return "user break";
    }
    return "exception";
}

/* The faulting thread resumes here (the handler rewrote its PC): ordinary thread context, so it may
   log, take mutexes and write the card. Other threads keep running; this one parks. */
static __attribute__((noreturn, used)) void db_crash_park(void) {
    const char *name = sCrash.thd ? sCrash.thd->label : "?";
    db_log("WINGS64_CRASH %s (code %03lx) in thread '%s'\n", exc_name(sCrash.code), (unsigned long)sCrash.code, name);
    db_log("  pc %08lx pr %08lx sr %08lx   (sh-elf-addr2line -f -e build/wings64.elf <pc> <pr>)\n",
           (unsigned long)sCrash.pc, (unsigned long)sCrash.pr, (unsigned long)sCrash.sr);
    for (int i = 0; i < 16; i += 4)
        db_log("  r%-2d %08lx %08lx %08lx %08lx\n", i, (unsigned long)sCrash.r[i], (unsigned long)sCrash.r[i + 1],
               (unsigned long)sCrash.r[i + 2], (unsigned long)sCrash.r[i + 3]);
    db_log("  stack %08lx: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)sCrash.r[15],
           (unsigned long)sCrash.stack[0], (unsigned long)sCrash.stack[1], (unsigned long)sCrash.stack[2],
           (unsigned long)sCrash.stack[3], (unsigned long)sCrash.stack[4], (unsigned long)sCrash.stack[5],
           (unsigned long)sCrash.stack[6], (unsigned long)sCrash.stack[7]);
    db_sd_flush("crash");
    for (;;) thd_sleep(1000);
}

static void db_exc_handler(irq_t code, irq_context_t *ctx, void *data) {
    (void)data;
    if (sCrash.active || (ctx->sr & 0x10000000u) /* BL: fault inside an exception handler */) {
        dbglog(DBG_CRITICAL, "WINGS64_CRASH nested (code %03lx pc %08lx)\n", (unsigned long)code, (unsigned long)ctx->pc);
        arch_abort();
    }
    sCrash.active = 1;
    sCrash.code = code; sCrash.pc = ctx->pc; sCrash.pr = ctx->pr; sCrash.sr = ctx->sr;
    memcpy(sCrash.r, ctx->r, sizeof sCrash.r);
    sCrash.thd = thd_get_current();
    {
        uint32_t sp = ctx->r[15];
        /* only read the stack if it is a plausible, aligned main-RAM address (MMU is off) */
        if ((sp & 3) == 0 && sp >= 0x8c000000u && sp + sizeof sCrash.stack <= 0x8d000000u)
            memcpy(sCrash.stack, (const void *)sp, sizeof sCrash.stack);
    }
    ctx->pc = (uint32_t)(uintptr_t)db_crash_park;
}

static void db_assert_handler(const char *file, int line, const char *expr, const char *msg, const char *func) {
    db_log("WINGS64_ASSERT %s:%d %s: %s%s%s\n", file, line, func ? func : "?", expr ? expr : "",
           msg ? " -- " : "", msg ? msg : "");
    db_sd_flush("assert");
    arch_abort();
}

static void crash_hook_install(void) {
    static const irq_t codes[] = { EXC_ILLEGAL_INSTR, EXC_SLOT_ILLEGAL_INSTR, EXC_DATA_ADDRESS_READ,
                                   EXC_DATA_ADDRESS_WRITE };
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++)
        if (!irq_get_handler(codes[i]).hdl) irq_set_handler(codes[i], db_exc_handler, NULL);
    assert_set_handler(db_assert_handler);
}
#endif

void db_init(void) {
#if DB_CRASH
    crash_hook_install();
#endif
#if DB_SD_BUILD
    if (sd_on) dbglog(DBG_INFO, "db: SD log on at boot: buffering in RAM; card comes up at %u ms\n", (unsigned)DB_LOG_SD_ARM_MS);
#endif
}
