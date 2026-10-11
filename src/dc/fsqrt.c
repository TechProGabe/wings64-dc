/* fsqrt.c — sqrtf on the SH-4's fsqrt instruction (docs/fps-plan.md; hardware profile 2026-10-11).
 * The decomp calls sqrtf from physics, camera and effects; newlib's sqrtf is a software bit loop
 * (ef_sqrt.c) plus an errno wrapper. fsqrt is IEEE correctly rounded in single precision, as newlib's is, so
 * every result is bit-identical (fdr replays unaffected) and no FSRRA approximation is involved (the
 * physics purity rule). KOS runs the FPU with denormals flushed to zero (FPSCR.DN), so a denormal input
 * would give 0: those few go to newlib's routine to stay bit-identical (DB_FSQRT_TEST checks 200k values).
 * Our definition replaces libm's at link time. errno is not set for negative input (the game never reads
 * it). Kill switch: -DWINGS64_FSQRT=0.
 */
#ifndef WINGS64_FSQRT
#define WINGS64_FSQRT 1
#endif

#if WINGS64_FSQRT
float __ieee754_sqrtf(float);
float sqrtf(float x) {
    union { float f; unsigned u; } b = { x };
    /* denormal (exponent 0, mantissa != 0): FPSCR.DN flushes it, even in a float compare, so test bits */
    if ((b.u & 0x7F800000u) == 0 && (b.u & 0x007FFFFFu) != 0) return __ieee754_sqrtf(x);
    __asm__("fsqrt %0" : "+f"(x));
    return x;
}
#endif

#if WINGS64_FSQRT && defined(DB_FSQRT_TEST)
/* Boot self-test (debug knob): fsqrt against newlib's software square root, bit for bit. */
#include "db.h"
#include <stdint.h>
#include <string.h>
void wings64_fsqrt_test(void) {
    uint32_t bad = 0, n = 0, s = 0x12345678u;
    for (uint32_t i = 0; i < 200000; i++) {
        uint32_t bits;
        float x, a, b;
        s = s * 1664525u + 1013904223u;
        bits = s & 0x7FFFFFFFu;                       /* positive, all exponents incl. denormals */
        if ((bits & 0x7F800000u) == 0x7F800000u) continue;   /* skip inf/NaN */
        memcpy(&x, &bits, 4);
        a = sqrtf(x);
        b = __ieee754_sqrtf(x);
        n++;
        if (memcmp(&a, &b, 4) != 0 && bad++ < 4) {
            uint32_t ab, bb;
            memcpy(&ab, &a, 4); memcpy(&bb, &b, 4);
            db_log("fsqrt: MISMATCH x %08lx fsqrt %08lx newlib %08lx\n", (unsigned long)bits, (unsigned long)ab,
                   (unsigned long)bb);
        }
    }
    db_log("fsqrt: %lu values, %lu mismatches vs newlib\n", (unsigned long)n, (unsigned long)bad);
}
#endif
