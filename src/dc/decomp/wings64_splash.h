/* wings64_splash.h — the "TECHPROGABE presents" splash (src/dc/splash.c), as seen from the decomp hook
 * (patches/025). Plain prototypes only: decomp TUs and port TUs both include it.
 *
 * WINGS64_SPLASH (default 1) is the kill switch: 0 compiles the splash out and the title opens as before.
 *   EXTRA_CFLAGS=-DWINGS64_SPLASH=0 ./run.sh build
 */
#ifndef WINGS64_SPLASH_H
#define WINGS64_SPLASH_H

#ifndef WINGS64_SPLASH
#define WINGS64_SPLASH 1
#endif

/* game.c, gameUpdateStateTitle, before the title's own level load. Runs once per boot: the Option
   screen's backdrop, the splash text, a fade in and out, skippable with A or Start. Returns 1 if it
   loaded a level (the caller then forces the title to reload its own). */
int wings64_splash_run(void);

#endif
