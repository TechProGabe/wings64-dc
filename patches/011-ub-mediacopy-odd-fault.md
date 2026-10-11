# 011-ub-mediacopy-odd-fault

**Why:** `_uvMediaCopy` deliberately stores to address 1 on an odd copy request (IDO split it into byte stores). On SH-4 that is an address-error exception with no message. Replaced with `pw64_fatal`.

**Origin:** own (kb/traps.md).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_DC` with the original in `#else`.
