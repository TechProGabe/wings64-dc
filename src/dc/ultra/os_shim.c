/* os_shim.c — the libultra OS/IO surface Pilotwings 64 calls, on KallistiOS (PLAN D5).
 * Compiled with the decomp's headers (decomp flags); KOS is reached through kos_bridge.h.
 *
 * Model: every N64 thread is a real KOS thread with a distinct, inverted priority; message queues
 * block for real; the VI event comes from the vblank interrupt; cart DMA is a copy from the
 * ROM-in-RAM image; SP tasks run synchronously in osSpTaskStartGo and post SP/DP done at once.
 * Locking is "interrupts off" (single core), which also makes NOBLOCK sends from the vblank IRQ safe.
 */
#include <ultra64.h>
#include <PR/os_internal.h>
#include "kos_bridge.h"

#define MAX_THREADS 16
#define KOS_STACK (64 * 1024)

/* ---------------------------------------------------------------- threads */

/* DIVERGES: KOS time-slices threads of equal priority and switches on its tick (raised to 1 kHz),
   not on every N64 interrupt; N64 threads of equal priority never preempt each other. */
static int kos_prio(OSPri pri) { return 20 + (OS_PRIORITY_MAX - (int)pri); }

typedef struct {
    OSThread *t;
    void *kthd;
    void (*entry)(void *);
    void *arg;
} dc_thread_t;
static dc_thread_t sThreads[MAX_THREADS];
static int sNumThreads;

static dc_thread_t *find_thread(OSThread *t) {
    for (int i = 0; i < sNumThreads; i++)
        if (sThreads[i].t == t) return &sThreads[i];
    return NULL;
}
static dc_thread_t *find_self(void) {
    void *self = kb_thread_self();
    for (int i = 0; i < sNumThreads; i++)
        if (sThreads[i].kthd == self) return &sThreads[i];
    return NULL;
}

void osCreateThread(OSThread *t, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri pri) {
    dc_thread_t *d = find_thread(t);
    (void)sp; /* DIVERGES: game stacks (8-16 KB) are ignored; each thread gets a 64 KB KOS stack */
    if (!d) {
        if (sNumThreads == MAX_THREADS) pw64_fatal("osCreateThread: too many threads", id);
        d = &sThreads[sNumThreads++];
    }
    d->t = t;
    d->kthd = NULL;
    d->entry = entry;
    d->arg = arg;
    t->id = id;
    t->priority = pri;
    t->state = OS_STATE_STOPPED;
}

void osStartThread(OSThread *t) {
    dc_thread_t *d = find_thread(t);
    if (!d) pw64_fatal("osStartThread: unknown thread", (u32)t);
    if (d->kthd) return;
    t->state = OS_STATE_RUNNABLE;
    d->kthd = kb_thread_spawn(d->entry, d->arg, kos_prio(t->priority), KOS_STACK, "n64");
    if (!d->kthd) pw64_fatal("osStartThread: KOS thread create failed", t->id);
    kb_yield(); /* N64: a higher-priority thread starts running immediately */
}

void osSetThreadPri(OSThread *t, OSPri pri) {
    dc_thread_t *d = t ? find_thread(t) : find_self();
    if (!d) return;
    d->t->priority = pri;
    if (d->kthd) kb_thread_set_prio(d->kthd, kos_prio(pri));
    kb_yield();
}

void osYieldThread(void) { kb_yield(); }

OSThread *__osGetCurrFaultedThread(void) { return NULL; }

/* --------------------------------------------------------------- messages */

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count) {
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

static s32 mesg_put(OSMesgQueue *mq, OSMesg msg, s32 flag, int jam) {
    int old = kb_irq_disable();
    while (mq->validCount >= mq->msgCount) {
        if (flag == OS_MESG_NOBLOCK || kb_inside_irq()) { kb_irq_restore(old); return -1; }
        kb_wait(&mq->fullqueue, 0);
    }
    if (jam) {
        mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
        mq->msg[mq->first] = msg;
    } else {
        mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    }
    mq->validCount++;
    kb_wake_all(&mq->mtqueue);
    kb_irq_restore(old);
    if (!kb_inside_irq()) kb_yield(); /* let a higher-priority receiver run now, as on the N64 */
    return 0;
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag) { return mesg_put(mq, msg, flag, 0); }
s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag) { return mesg_put(mq, msg, flag, 1); }

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag) {
    int old = kb_irq_disable();
    while (mq->validCount == 0) {
        if (flag == OS_MESG_NOBLOCK) { kb_irq_restore(old); return -1; }
        kb_wait(&mq->mtqueue, 0);
    }
    if (msg) *msg = mq->msg[mq->first];
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    kb_wake_all(&mq->fullqueue);
    kb_irq_restore(old);
    return 0;
}

/* ----------------------------------------------------------------- events */

static struct { OSMesgQueue *mq; OSMesg msg; } sEvents[OS_NUM_EVENTS];

void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg) {
    if (e < OS_NUM_EVENTS) { sEvents[e].mq = mq; sEvents[e].msg = msg; }
}
static void post_event(OSEvent e) {
    if (sEvents[e].mq) osSendMesg(sEvents[e].mq, sEvents[e].msg, OS_MESG_NOBLOCK);
}

OSIntMask osSetIntMask(OSIntMask m) { (void)m; return OS_IM_ALL; }

/* ------------------------------------------------------------- time/timers */

/* N64 CPU counter: 46.875 MHz = 3/64 counts per ns. */
u32 osGetCount(void) { return (u32)(kb_time_ns() * 3 / 64); }
static u64 now_cycles(void) { return kb_time_ns() * 3 / 64; }

#define MAX_TIMERS 8
static struct { OSTimer *t; u64 due; } sTimers[MAX_TIMERS];
static int sTimerKick;
static int sTimerThreadUp;

static void timer_thread(void *arg) {
    (void)arg;
    for (;;) {
        int old = kb_irq_disable();
        u64 now = now_cycles(), next = ~0ull;
        for (int i = 0; i < MAX_TIMERS; i++) {
            OSTimer *t = sTimers[i].t;
            if (!t) continue;
            if (sTimers[i].due <= now) {
                osSendMesg(t->mq, t->msg, OS_MESG_NOBLOCK);
                if (t->interval) sTimers[i].due += t->interval;
                else sTimers[i].t = NULL;
            }
            if (sTimers[i].t && sTimers[i].due < next) next = sTimers[i].due;
        }
        {
            int ms = next == ~0ull ? 0 : (int)((next - now) * 64 / 3 / 1000000) + 1;
            kb_wait(&sTimerKick, ms);
        }
        kb_irq_restore(old);
    }
}

int osSetTimer(OSTimer *t, OSTime countdown, OSTime interval, OSMesgQueue *mq, OSMesg msg) {
    int old, slot = -1;
    if (!sTimerThreadUp) { sTimerThreadUp = 1; kb_thread_spawn(timer_thread, NULL, 15, 16 * 1024, "n64-timer"); }
    t->interval = interval;
    t->value = countdown;
    t->mq = mq;
    t->msg = msg;
    old = kb_irq_disable();
    for (int i = 0; i < MAX_TIMERS; i++)
        if (sTimers[i].t == NULL || sTimers[i].t == t) { slot = i; break; }
    if (slot < 0) pw64_fatal("osSetTimer: out of timers", MAX_TIMERS);
    sTimers[slot].t = t;
    sTimers[slot].due = now_cycles() + (countdown ? countdown : interval);
    kb_wake_all(&sTimerKick);
    kb_irq_restore(old);
    return 0;
}

/* ------------------------------------------------------------ cache/addr */

/* Cart DMA is a CPU copy and the PVR path flushes what it reads: no N64 cache maintenance needed. */
void osWritebackDCache(void *p, s32 n) { (void)p; (void)n; }
void osWritebackDCacheAll(void) {}
void osInvalDCache(void *p, s32 n) { (void)p; (void)n; }
u32 osVirtualToPhysical(void *p) { return (u32)p & 0x1FFFFFFF; }

/* ---------------------------------------------------------------------- VI */

static OSMesgQueue *sViMq;
static OSMesg sViMsg;
static u32 sViEvery = 1, sRetrace;
static void *sViCur, *sViNext;
static int sViKick;
static unsigned sVblanks;

static void vblank_irq(void) {
    sVblanks++;
    kb_wake_all(&sViKick);
}

static void vi_thread(void *arg) {
    unsigned seen = 0;
    (void)arg;
    for (;;) {
        int old = kb_irq_disable();
        while (seen == sVblanks) kb_wait(&sViKick, 0);
        seen = sVblanks;
        kb_irq_restore(old);
        sViCur = sViNext;                       /* the swap latches at the retrace */
        sRetrace++;
        if (sViMq && sRetrace % sViEvery == 0) osSendMesg(sViMq, sViMsg, OS_MESG_NOBLOCK);
    }
}

void osCreateViManager(OSPri pri) {
    static int up;
    if (up) return;
    up = 1;
    kb_thread_spawn(vi_thread, NULL, kos_prio(pri), 16 * 1024, "n64-vi");
    kb_vblank_hook(vblank_irq);
}
void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount) {
    sViMq = mq; sViMsg = msg; sViEvery = retraceCount ? retraceCount : 1;
}
void osViSwapBuffer(void *fb) { sViNext = fb; if (!sViCur) sViCur = fb; }
void *osViGetCurrentFramebuffer(void) { return sViCur; }
void *osViGetNextFramebuffer(void) { return sViNext; }
void osViSetMode(OSViMode *m) { (void)m; }          /* video mode is the PVR's (main.c) */
void osViBlack(u8 on) { (void)on; }
void osViSetSpecialFeatures(u32 f) { (void)f; }
u32 wings64_vi_retraces(void) { return sRetrace; }

/* ---------------------------------------------------------------------- PI */

void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdMsgCnt) {
    (void)pri; (void)cmdQ; (void)cmdBuf; (void)cmdMsgCnt;
}

static u32 sPiZeroReads;

s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 dir, u32 devAddr, void *vAddr, u32 nbytes, OSMesgQueue *mq) {
    const void *src;
    (void)pri;
    if (dir != OS_READ) pw64_fatal("osPiStartDma: write to cart", devAddr);
    src = kb_rom_ptr(devAddr, nbytes);
    if (src) {
        kb_memcpy(vAddr, src, nbytes);
    } else {
        /* DIVERGES: audio_tbl (ROM 0x6314D0+) is not in the RAM image; sample DMA reads silence until
           the AICA driver (M4) plays waves itself. */
        kb_memset(vAddr, 0, nbytes);
        sPiZeroReads++;
    }
    mb->hdr.retQueue = mq;
    mb->dramAddr = vAddr;
    mb->devAddr = devAddr;
    mb->size = nbytes;
    if (mq) osSendMesg(mq, (OSMesg)mb, OS_MESG_NOBLOCK);
    return 0;
}

/* Every caller stores the word and then reads it as bytes (_uvMediaCopy's unaligned head, bootproc's
   debug-area probe). On the big-endian N64 that byte image is the ROM's byte order, so the word is
   copied raw, not converted to its numeric big-endian value. */
static s32 pi_read_word(u32 devAddr, u32 *data) {
    const u8 *p = kb_rom_ptr(devAddr & ~3u, 4);
    if (p) kb_memcpy(data, p, 4);
    else *data = 0;
    return 0;
}
s32 osPiReadIo(u32 devAddr, u32 *data) { return pi_read_word(devAddr, data); }
s32 osPiRawReadIo(u32 devAddr, u32 *data) { return pi_read_word(devAddr, data); }

/* ---------------------------------------------------------------------- SP */

static u32 sGfxTasks, sAudTasks;

/* The display-list interpreter (src/dc/gfx/gfx_f3d.c). Weak so the host test build links without it. */
__attribute__((weak)) void gfx_f3d_run(const void *dl) { (void)dl; }

void osSpTaskLoad(OSTask *task) { (void)task; }
void osSpTaskStartGo(OSTask *task) {
    if (task->t.type == M_GFXTASK) {
        sGfxTasks++;
        gfx_f3d_run(task->t.data_ptr);
        post_event(OS_EVENT_SP);
        post_event(OS_EVENT_DP);
    } else {
        sAudTasks++;  /* DIVERGES: audio command lists are discarded; the AICA driver (M4) replaces aspMain */
        post_event(OS_EVENT_SP);
    }
}
void osSpTaskYield(void) {}
OSYieldResult osSpTaskYielded(OSTask *task) { (void)task; return 0; }
u32 wings64_gfx_tasks(void) { return sGfxTasks; }
u32 wings64_aud_tasks(void) { return sAudTasks; }
u32 wings64_pi_zero_reads(void) { return sPiZeroReads; }

/* ---------------------------------------------------------------------- AI */

s32 osAiSetFrequency(u32 f) { return (s32)f; }
s32 osAiSetNextBuffer(void *buf, u32 size) { (void)buf; (void)size; return 0; }
u32 osAiGetLength(void) { return 0; }

/* ---------------------------------------------------------------------- SI */

/* KOS CONT_* bits (kos_bridge users must not include KOS headers here). */
#define DC_C (1u << 0)
#define DC_B (1u << 1)
#define DC_A (1u << 2)
#define DC_START (1u << 3)
#define DC_UP (1u << 4)
#define DC_DOWN (1u << 5)
#define DC_LEFT (1u << 6)
#define DC_RIGHT (1u << 7)
#define DC_Y (1u << 9)
#define DC_X (1u << 10)

static OSContPad sPads[MAXCONTROLLERS];

/* Optional scripted input for the emulator gate (DB_AUTOPLAY, src/dc/autoplay.c). */
__attribute__((weak)) int wings64_autoplay(u32 poll, u16 *buttons, s8 *sx, s8 *sy) {
    (void)poll; (void)buttons; (void)sx; (void)sy; return 0;
}

static s8 stick(int v) {
    int s = v * 80 / 128;
    return (s8)(s > 80 ? 80 : s < -80 ? -80 : s);
}

static void read_pads(void) {
    static u32 polls;
    for (int i = 0; i < MAXCONTROLLERS; i++) {
        kb_pad_t p;
        u16 b = 0;
        kb_pad_read(i, &p);
        sPads[i].errno = p.present ? 0 : CONT_NO_RESPONSE_ERROR;
        if (p.present) {
            /* PLAN M2 map: A->A, B->B, X->C-left, Y->C-right, D-pad->D-pad (+C-up/C-down),
               L trigger->Z, R trigger->R, Start->Start, stick x80/128 with Y up positive. */
            if (p.buttons & DC_A) b |= A_BUTTON;
            if (p.buttons & DC_B) b |= B_BUTTON;
            if (p.buttons & DC_X) b |= L_CBUTTONS;
            if (p.buttons & DC_Y) b |= R_CBUTTONS;
            if (p.buttons & DC_START) b |= START_BUTTON;
            if (p.buttons & DC_UP) b |= U_JPAD | U_CBUTTONS;
            if (p.buttons & DC_DOWN) b |= D_JPAD | D_CBUTTONS;
            if (p.buttons & DC_LEFT) b |= L_JPAD;
            if (p.buttons & DC_RIGHT) b |= R_JPAD;
            if (p.ltrig >= 128) b |= Z_TRIG;
            if (p.rtrig >= 128) b |= R_TRIG;
            sPads[i].stick_x = stick(p.joyx);
            sPads[i].stick_y = stick(-p.joyy);
        } else {
            sPads[i].stick_x = sPads[i].stick_y = 0;
        }
        if (i == 0 && wings64_autoplay(polls, &b, &sPads[0].stick_x, &sPads[0].stick_y)) sPads[0].errno = 0;
        sPads[i].button = b;
    }
    polls++;
}

s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *status) {
    u8 bits = 0;
    (void)mq;
    read_pads();
    for (int i = 0; i < MAXCONTROLLERS; i++) {
        int present = i == 0 || sPads[i].errno == 0;   /* port A always reports (autoplay, hot-plug) */
        status[i].type = CONT_TYPE_NORMAL;
        status[i].status = 0;
        status[i].errno = present ? 0 : CONT_NO_RESPONSE_ERROR;
        if (present) bits |= 1 << i;
    }
    *bitpattern = bits;
    return 0;
}
s32 osContStartReadData(OSMesgQueue *mq) {
    (void)mq;
    read_pads();
    post_event(OS_EVENT_SI);
    return 0;
}
void osContGetReadData(OSContPad *pads) {
    for (int i = 0; i < MAXCONTROLLERS; i++) pads[i] = sPads[i];
}

/* ------------------------------------------------------------------ EEPROM */

#define EEPROM_BYTES 0x800 /* 16 Kbit */
static u8 sEeprom[EEPROM_BYTES];
static int sEepromLoaded;

/* VMU persistence (src/dc/save/vmu_eeprom.c). Weak until linked: RAM only. */
__attribute__((weak)) int vmu_eeprom_load(unsigned char *img) { (void)img; return 0; }
__attribute__((weak)) int vmu_eeprom_save(const unsigned char *img) { (void)img; return 0; }

s32 osEepromProbe(OSMesgQueue *mq) {
    (void)mq;
    if (!sEepromLoaded) { sEepromLoaded = 1; vmu_eeprom_load(sEeprom); }
    return EEPROM_TYPE_16K;
}
s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes) {
    (void)mq;
    if (address * 8 + nbytes > EEPROM_BYTES) return -1;
    kb_memcpy(buffer, &sEeprom[address * 8], nbytes);
    return 0;
}
s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes) {
    (void)mq;
    if (address * 8 + nbytes > EEPROM_BYTES) return -1;
    kb_memcpy(&sEeprom[address * 8], buffer, nbytes);
    vmu_eeprom_save(sEeprom);   /* DIVERGES: every EEPROM write is a full VMU save (no write-behind yet) */
    return 0;
}

/* -------------------------------------------------------------------- init */

void osInitialize(void) {
    kb_set_hz(1000);
}
