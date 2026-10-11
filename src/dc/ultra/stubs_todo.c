/* stubs_todo.c — M1 link stubs for the libultra OS/IO surface the game calls.
 * Each stops with "WINGS64_FATAL TODO <name>" if reached; M2 replaces them one by one with the real
 * shim in src/dc/ultra/ (threads, mesg queues, VI/PI/SI/SP/AI, EEPROM->VMU) and deletes the line here.
 * Deliberately header-free: the real prototypes live in the decomp headers, so these are declared
 * (void) and only ever called through the decomp's own declarations.
 */
__attribute__((noreturn)) void pw64_fatal(const char *msg, unsigned int value);

#define TODO(name) \
    void name(void); \
    void name(void) { pw64_fatal("TODO " #name, 0); }

/* Threads, messages, events, timers, interrupts */
TODO(osInitialize)
TODO(osCreateThread)
TODO(osStartThread)
TODO(osSetThreadPri)
TODO(osYieldThread)
TODO(osCreateMesgQueue)
TODO(osSendMesg)
TODO(osRecvMesg)
TODO(osSetEventMesg)
TODO(osSetIntMask)
TODO(osSetTimer)
TODO(osGetCount)
TODO(__osGetCurrFaultedThread)
/* Cache / address */
TODO(osVirtualToPhysical)
TODO(osWritebackDCache)
TODO(osWritebackDCacheAll)
TODO(osInvalDCache)
/* VI */
TODO(osCreateViManager)
TODO(osViSetMode)
TODO(osViSetEvent)
TODO(osViSetSpecialFeatures)
TODO(osViBlack)
TODO(osViSwapBuffer)
TODO(osViGetCurrentFramebuffer)
TODO(osViGetNextFramebuffer)
/* PI (cart DMA) */
TODO(osCreatePiManager)
TODO(osPiStartDma)
TODO(osPiReadIo)
TODO(osPiRawReadIo)
/* SP (RSP tasks) */
TODO(osSpTaskLoad)
TODO(osSpTaskStartGo)
TODO(osSpTaskYield)
TODO(osSpTaskYielded)
/* AI */
TODO(osAiSetFrequency)
TODO(osAiSetNextBuffer)
TODO(osAiGetLength)
/* SI: controllers and EEPROM */
TODO(osContInit)
TODO(osContStartReadData)
TODO(osContGetReadData)
TODO(osEepromProbe)
TODO(osEepromLongRead)
TODO(osEepromLongWrite)
