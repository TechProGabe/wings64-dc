/* hoststubs/kos.h — the slice of the KallistiOS API that host-built port TUs use, over POSIX stdio.
 * Host tests only (src/dc/test). Grows as M2+ port code gets host coverage. */
#ifndef WINGS64_HOST_KOS_H
#define WINGS64_HOST_KOS_H

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int file_t;

static inline file_t fs_open(const char *fn, int mode) { return open(fn, mode, 0644); }
static inline int fs_close(file_t f) { return close(f); }
static inline ssize_t fs_read(file_t f, void *buf, size_t n) { return read(f, buf, n); }
static inline ssize_t fs_write(file_t f, const void *buf, size_t n) { return write(f, buf, n); }
static inline size_t fs_total(file_t f) { struct stat st; return fstat(f, &st) ? 0 : (size_t)st.st_size; }
static inline int fs_mkdir(const char *fn) { return mkdir(fn, 0755); }

#define DBG_CRITICAL 1
#define DBG_INFO 6
static inline void dbglog(int level, const char *fmt, ...) {
    va_list ap; (void)level;
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
static inline uint64_t timer_ms_gettime64(void) { return 0; }
static inline void arch_abort(void) { abort(); }

#endif
