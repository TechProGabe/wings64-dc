/* db.c — debug log sink: KOS dbglog (serial / dcload) plus the optional SD mirror. */
#include "db.h"
#include "stamp.h"

#include <kos.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sh4zam/shz_sh4zam.h>

const char wings64_build_id[] = WINGS64_BUILD_ID;

#if DB_LOG_SD
#include <dc/sd.h>
#include <fat/fs_fat.h>

#define SD_BUF_SIZE (64 * 1024)
#define SD_LOG_CAP  (8u * 1024u * 1024u)
static char sd_buf[SD_BUF_SIZE];
static size_t sd_len;
static size_t sd_total;
static int sd_ok;
static char sd_path[96];
static kos_blockdev_t sd_dev;

int db_sd_init(void) {
    uint8_t ptype;
    if (sd_ok) return 1;
    if (sd_init() || sd_blockdev_for_partition(0, &sd_dev, &ptype) || fs_fat_init() ||
        fs_fat_mount("/sd", &sd_dev, FS_FAT_MOUNT_READWRITE)) {
        dbglog(DBG_INFO, "db: no SD card log\n");
        return 0;
    }
    fs_mkdir("/sd/wings64");
    snprintf(sd_path, sizeof sd_path, "/sd/wings64/log-%s.txt", wings64_build_id);
    sd_ok = 1;
    dbglog(DBG_INFO, "db: SD log %s\n", sd_path);
    return 1;
}

/* Open/append/close each flush: slow but survives a power-off at any point. */
void db_sd_flush(void) {
    file_t f;
    if (!sd_ok || sd_len == 0) return;
    f = fs_open(sd_path, O_WRONLY | O_APPEND | O_CREAT);
    if (f >= 0) {
        fs_write(f, sd_buf, sd_len);
        fs_close(f);
    }
    sd_total += sd_len;
    sd_len = 0;
    if (sd_total >= SD_LOG_CAP) sd_ok = 0;
}

static void sd_append(const char *s, size_t n) {
    if (!sd_ok) return;
    if (sd_len + n > SD_BUF_SIZE) db_sd_flush();
    if (n > SD_BUF_SIZE) n = SD_BUF_SIZE;
    shz_memcpy(sd_buf + sd_len, s, n);
    sd_len += n;
}
#else
int db_sd_init(void) { return 0; }
void db_sd_flush(void) {}
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
    dbglog(DBG_INFO, "%s", line);
#if DB_LOG_SD
    sd_append(line, (size_t)n);
#endif
}
#endif
