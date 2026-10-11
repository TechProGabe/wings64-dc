# 015-endian-audio-bank-loader

**Why:** `alBnkfNew`/`alSeqFileNew` patch offsets into pointers in place inside a raw big-endian copy. They are replaced by field-wise deserialisers (`pw64_alBnkfLoad`, `pw64_alSeqFileLoad`, src/dc/rom/pw64_audio_load.c) that build host-order structs.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
