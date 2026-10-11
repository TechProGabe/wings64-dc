# 001-bitfield-uv-filesystem

**Why:** IDO packs bitfields MSB-first; GCC on little-endian SH-4 packs LSB-first. The u16 flag word in `uv_filesystem.h` (format = (flags>>8)&7, flag = flags&0x800) is declared in reverse order so field reads match the ROM bytes.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
