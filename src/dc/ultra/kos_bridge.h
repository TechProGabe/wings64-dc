/* kos_bridge.h — the KallistiOS services the libultra shim needs, in plain C types.
 * os_shim.c is compiled with the decomp's headers and libc; kos_bridge.c with KOS's. Neither can
 * include the other's headers, so this header includes nothing.
 */
#ifndef WINGS64_KOS_BRIDGE_H
#define WINGS64_KOS_BRIDGE_H

/* Threads. KOS priority: lower number = runs first. */
void *kb_thread_spawn(void (*entry)(void *), void *arg, int kos_prio, unsigned stack_size, const char *label);
void *kb_thread_self(void);
void kb_thread_set_prio(void *kthd, int kos_prio);
void kb_yield(void);
void kb_set_hz(unsigned hz);
unsigned long long kb_thread_cpu_ns(void *kthd);   /* CPU time the thread has run */
unsigned long long kb_total_cpu_ns(void);

/* Global lock = interrupts off (single core). kb_wait must be called with interrupts off; it blocks,
   then returns with them still off. timeout_ms 0 = forever. */
int kb_irq_disable(void);
void kb_irq_restore(int old);
int kb_inside_irq(void);
void kb_wait(void *obj, int timeout_ms);
void kb_wake_all(void *obj);

unsigned long long kb_time_ns(void);
void kb_vblank_hook(void (*fn)(void));

/* ROM-in-RAM: pointer to `size` bytes at ROM address `addr`, or 0 if outside the loaded slice. */
const void *kb_rom_ptr(unsigned addr, unsigned size);

/* Controllers: KOS cont_state_t essentials. present = 0 if no controller in that port. */
typedef struct {
    int present;
    unsigned buttons;   /* KOS CONT_* bits */
    int ltrig, rtrig;   /* 0..255 */
    int joyx, joyy;     /* -128..127, y down positive */
} kb_pad_t;
void kb_pad_read(int port, kb_pad_t *out);

/* Memory moves for decomp-side code (shz_memcpy per docs/sh4zam-mandate.md N.4). */
void kb_memcpy(void *dst, const void *src, unsigned n);
void kb_memset(void *dst, int v, unsigned n);

void kb_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
