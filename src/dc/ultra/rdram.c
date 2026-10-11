/* rdram.c — the 4 MB "RDRAM" arena that stands in for the N64's fixed memory map (patches/017).
 * N64_K0(a) = gWings64RdramBase + (a - 0x80000000). Allocated once before bootproc.
 * DIVERGES: the N64 code/data window 0x80200000..0x80380000 is reserved in the arena but unused (the
 * code lives in the ELF); ~1.5 MB to reclaim later (kb/levers.md).
 */
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

__attribute__((noreturn)) void pw64_fatal(const char *msg, unsigned int value);

#define N64_RDRAM_SIZE 0x400000u

unsigned int gWings64RdramBase;

void wings64_rdram_init(void) {
    void *p;
    if (gWings64RdramBase) return;
    p = memalign(64, N64_RDRAM_SIZE);      /* 64: covers 8-byte DMA and 32-byte cache-line users */
    if (!p) pw64_fatal("rdram: cannot allocate the 4 MB N64 arena", N64_RDRAM_SIZE);
    memset(p, 0, N64_RDRAM_SIZE);
    gWings64RdramBase = (unsigned int)p;
}
