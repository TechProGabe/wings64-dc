# 012-yield-busy-waits

**Why:** Busy-wait spins on the app clock (`snap.c` x3, `title_screen.c`, `uvaSeqStop`) relied on N64 preemption by the scheduler/audio threads. KOS only round-robins equal priorities, so the spins yield with `osYieldThread()` (PLAN D5, H4).

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
