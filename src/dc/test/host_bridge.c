/* host_bridge.c — kos_bridge.h for the ILP32 host test build. Host tests exercise data paths (ROM,
 * swaps, parsers), not threading: anything that would need a scheduler stops with pw64_fatal. */
#include "ultra/kos_bridge.h"
#include "rom/romfs.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

__attribute__((noreturn)) void pw64_fatal(const char *msg, unsigned int value);

void *kb_thread_spawn(void (*entry)(void *), void *arg, int kos_prio, unsigned stack_size, const char *label) {
    (void)entry; (void)arg; (void)kos_prio; (void)stack_size;
    pw64_fatal(label, 0);
}
void *kb_thread_self(void) { return (void *)1; }
void kb_thread_set_prio(void *kthd, int kos_prio) { (void)kthd; (void)kos_prio; }
void kb_yield(void) {}
void kb_set_hz(unsigned hz) { (void)hz; }
int kb_irq_disable(void) { return 0; }
void kb_irq_restore(int old) { (void)old; }
int kb_inside_irq(void) { return 0; }
void kb_wait(void *obj, int timeout_ms) { (void)obj; (void)timeout_ms; pw64_fatal("kb_wait on host (would block)", 0); }
void kb_wake_all(void *obj) { (void)obj; }
unsigned long long kb_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}
void kb_vblank_hook(void (*fn)(void)) { (void)fn; }
const void *kb_rom_ptr(unsigned addr, unsigned size) {
    const unsigned char *p = romfs_rom_ptr(addr);
    if (!p || addr + size > ROMFS_ROM_START + romfs_size) return NULL;
    return p;
}
void kb_pad_read(int port, kb_pad_t *out) { (void)port; memset(out, 0, sizeof *out); }
void kb_memcpy(void *dst, const void *src, unsigned n) { memcpy(dst, src, n); }
void kb_memset(void *dst, int v, unsigned n) { memset(dst, v, n); }
void kb_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
