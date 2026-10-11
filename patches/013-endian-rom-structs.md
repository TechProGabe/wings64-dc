# 013-endian-rom-structs

**Why:** Big-endian ROM structs copied raw with `_uvMediaCopy`/`uvConsumeBytes` are swapped in place right after the copy with `PW64_SWAP` (layout-checked, src/dc/rom/pw64_swap.c): TABL index, UVEN, UVMD/UVCT Vtx and Mtx tables, UVTX Gfx words, UVLV/UVTR tables, UVBT u64 reads, tasks, spaths, levels, anims, user paths, demos, text, level block counts, and the code_A8C30 byte table (PLAN D3, H1).

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
