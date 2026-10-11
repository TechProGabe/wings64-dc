# 009-ub-synth-param-pool-stride

**Why:** `alSynNew` sizes its param pool with `sizeof(ALParam)` but stores `ALStartParamAlt` (larger on some ABIs) into it. Pool stride uses the larger struct. Harmless where both are 28 bytes; kept as a guard for the SH-4 ABI.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
