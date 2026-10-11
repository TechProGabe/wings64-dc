# 002-bitfield-setimg-cmd

**Why:** `_uvExpandTexture` finds G_SETTIMG commands by reading the `setimg.cmd` bitfield. With LSB-first bitfields that reads the wrong byte, so the opcode is read as `words.w0 >> 24` (PLAN H1, kb/traps.md).

**Origin:** own (Birdman64 does the same inside a 64-bit hunk).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
