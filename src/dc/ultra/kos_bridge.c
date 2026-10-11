/* kos_bridge.c — KallistiOS side of the libultra shim (see kos_bridge.h). */
#include "kos_bridge.h"
#include "../db.h"
#include "../rom/romfs.h"

#include <kos.h>
#include <arch/irq.h>
#include <dc/vblank.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sh4zam/shz_sh4zam.h>

typedef struct { void (*entry)(void *); void *arg; } tramp_t;
static tramp_t tramps[32];
static int ntramps;

static void *thread_tramp(void *p) {
    tramp_t *t = p;
    t->entry(t->arg);
    return NULL;
}

/* CPU time a KOS thread has run (ns), for the per-thread budget log; 0 for NULL. */
unsigned long long kb_thread_cpu_ns(void *kthd) {
    return kthd ? thd_get_cpu_time((kthread_t *)kthd) : 0;
}
unsigned long long kb_total_cpu_ns(void) { return thd_get_total_cpu_time(); }

void *kb_thread_spawn(void (*entry)(void *), void *arg, int kos_prio, unsigned stack_size, const char *label) {
    kthread_attr_t attr = { 0 };
    tramp_t *t;
    if (ntramps == (int)(sizeof tramps / sizeof tramps[0])) return NULL;
    t = &tramps[ntramps++];
    t->entry = entry;
    t->arg = arg;
    attr.create_detached = true;
    attr.stack_size = stack_size;
    attr.prio = (prio_t)kos_prio;
    attr.label = label;
    return thd_create_ex(&attr, thread_tramp, t);
}

void *kb_thread_self(void) { return thd_get_current(); }
void kb_thread_set_prio(void *kthd, int kos_prio) { thd_set_prio((kthread_t *)kthd, (prio_t)kos_prio); }
void kb_yield(void) { thd_pass(); }
void kb_set_hz(unsigned hz) { thd_set_hz(hz); }

int kb_irq_disable(void) { return irq_disable(); }
void kb_irq_restore(int old) { irq_restore(old); }
int kb_inside_irq(void) { return irq_inside_int(); }
void kb_wait(void *obj, int timeout_ms) { genwait_wait(obj, "ultra", timeout_ms, NULL); }
void kb_wake_all(void *obj) { genwait_wake_all(obj); }

unsigned long long kb_time_ns(void) { return timer_ns_gettime64(); }

static void (*vblank_fn)(void);
static void vblank_tramp(uint32_t code, void *data) { (void)code; (void)data; if (vblank_fn) vblank_fn(); }
void kb_vblank_hook(void (*fn)(void)) {
    vblank_fn = fn;
    vblank_handler_add(vblank_tramp, NULL);
}

const void *kb_rom_ptr(unsigned addr, unsigned size) {
    const uint8_t *p = romfs_rom_ptr(addr);
    if (!p || addr + size > ROMFS_ROM_START + romfs_size) return NULL;
    return p;
}

void kb_pad_read(int port, kb_pad_t *out) {
    maple_device_t *dev = maple_enum_type(port, MAPLE_FUNC_CONTROLLER);
    cont_state_t *st = dev ? (cont_state_t *)maple_dev_status(dev) : NULL;
    out->present = st != NULL;
    if (!st) { out->buttons = 0; out->ltrig = out->rtrig = out->joyx = out->joyy = 0; return; }
    out->buttons = st->buttons;
    out->ltrig = st->ltrig;
    out->rtrig = st->rtrig;
    out->joyx = st->joyx;
    out->joyy = st->joyy;
}

void kb_memcpy(void *dst, const void *src, unsigned n) { shz_memcpy(dst, src, n); }
void kb_memset(void *dst, int v, unsigned n) { memset(dst, v, n); }

void kb_log(const char *fmt, ...) {
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    db_log("%s", line);
}
