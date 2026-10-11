/* prof.h — PC-sampling profiler (prof.c). DB_PROF=1 builds it (hardware discs: ./run.sh hwdisc). */
#ifndef WINGS64_PROF_H
#define WINGS64_PROF_H

#ifndef DB_PROF
#define DB_PROF 0
#endif
#ifndef DB_PROF_AUTO_S
#define DB_PROF_AUTO_S 0      /* also dump this many seconds into the game (0 = chord only) */
#endif

#if DB_PROF
void prof_init(void);
void prof_poll(unsigned ms);
unsigned prof_dumps(void);
#else
static inline void prof_init(void) {}
static inline void prof_poll(unsigned ms) { (void)ms; }
static inline unsigned prof_dumps(void) { return 0; }
#endif

#endif
