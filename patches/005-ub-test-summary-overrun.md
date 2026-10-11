# 005-ub-test-summary-overrun

**Why:** Decomp UB: `textFmtInt` writes two terminators past `sp5C[3]`. Array grown to 3+2; the length argument stays 3.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
