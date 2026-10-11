# 016-endian-font-bitmaps

**Why:** UVFT font `FRMT` words and `BITM` Bitmap records are big-endian; read through `uvMemRead` and `pw64_load_bitmaps` (src/dc/rom/pw64_rom_structs.c).

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
