# 006-ub-sicontstatus-array

**Why:** `osContInit` writes MAXCONTROLLERS (4) `OSContStatus` entries; the decomp declares one. IDO left slack after it; GCC packs globals. Declared as `[4]`.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
