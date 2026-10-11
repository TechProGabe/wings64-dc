# 014-endian-texel-tint

**Why:** The time-of-day tint loops (`code_8170.c`) read and write texels as u16 while images stay big-endian in RAM. 16-bit images are swapped to host order around each loop and back. The Birdman64 `PW64_NATIVE` switch is dropped (WINGS64_DC guards the whole change).

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
