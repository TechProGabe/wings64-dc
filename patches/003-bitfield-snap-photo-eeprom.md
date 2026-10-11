# 003-bitfield-snap-photo-eeprom

**Why:** The EEPROM photo album stores the first 172 bits of each `Unk8033F050` as IDO lays it out (big-endian, MSB-first bitfields, 0x18 bytes). GCC lays the struct out differently, so explicit to/from IDO byte-image helpers build the saved bits; unset fields are zeroed so saves are deterministic.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
