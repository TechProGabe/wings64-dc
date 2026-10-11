# 010-ub-debug-vtx-colour-overrun

**Why:** `uvDbg_802339B0` colours 0x26 vertices through `D_8024B348` (14 entries), relying on IDO placing four arrays back to back. GCC gives no layout guarantee (`-Waggressive-loop-optimizations` flags it). Each array is walked explicitly; a static assert pins the 0x26 total.

**Origin:** own.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
