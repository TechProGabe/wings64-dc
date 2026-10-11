# 008-ub-reverb-unsigned-index

**Why:** `&r->input[-d->input]` negates a u32 index. Cast to s32 before negating so the pointer arithmetic is defined.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (the 64-bit-only PW64_U32 in the ramalign line dropped).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
