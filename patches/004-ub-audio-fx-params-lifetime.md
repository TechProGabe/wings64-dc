# 004-ub-audio-fx-params-lifetime

**Why:** Decomp UB: block-scope `customFxParams` is referenced through `gALSynConfig.params` after its lifetime ends. Made `static`.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
