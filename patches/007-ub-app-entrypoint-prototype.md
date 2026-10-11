# 007-ub-app-entrypoint-prototype

**Why:** `app_entrypoint` is defined with no parameters (map3d.c) but declared and called with an `s32`. Prototype and call fixed.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
