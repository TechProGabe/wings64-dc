# 025-port-splash

**Why:** the port opens with a "TECHPROGABE presents" card before the game's title screen. The card is
drawn with the game's own parts (the Option screen's sky backdrop and letterbox, font 6's bold set, the
game's fades), so it reads as part of the game. The card itself is port code in `src/dc/splash.c`.

**What:**
- `app/game.c`: `gameUpdateStateTitle` calls `wings64_splash_run()` before its own work. It runs once per
  boot (later returns to the title skip it). If the splash loaded its level, `D_8034F160` is reset so
  the title reloads its own level in the same call. The title's intro and menu are unchanged.
- The splash is about 2.4 s: 0.4 s fade in, 1.6 s hold, 0.5 s fade out (`func_8032D51C(0)`). A or Start
  skips it.

**Kill switch:** `WINGS64_SPLASH` (default 1, `src/dc/decomp/wings64_splash.h`).
`EXTRA_CFLAGS=-DWINGS64_SPLASH=0 ./run.sh build` compiles the call out.

**Origin:** own.

**N64 build:** unchanged (the hook is `#if defined(WINGS64_DC) && WINGS64_SPLASH`; the include is DC-only).
