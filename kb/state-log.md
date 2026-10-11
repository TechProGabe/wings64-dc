# state-log — evidence, newest first

## 2026-10-10 — M0 built (build 574fd2b-1011-0141); hardware boot pending
- Container: debian bookworm → dc-chain profile 14.3.0 (sh only, m4-single + m4-single-only multilibs) →
  KOS v2.2.2 with KOS_SH4_PRECISION=-m4-single-only → mkdcdisc v0.0.4. ~25 min on 6 colima CPUs.
- ROM: SHA-1 ec771aed…191a verified; slice ROM[0xDE720,0x6314D0) = 5,582,256 B, crc32 DAFCC147.
  TABL (GZIP/MIO0, 10176 B raw) → 1272 entries; Python and C MIO0 decoders agree byte-for-byte on host;
  filesys walk ends at 0x618B6C exactly.
- `./run.sh build`: 0 warnings; objdump shows 0 fcnvsd/fcnvds (no double conversions); no KOS mat_* linked.
  sh4zam backend compile-time asserted SH4.
- `./run.sh disc` → build/wings64.cdi 705 MB raw (padding on); `--emu` → 7 MB. Data track staged with
  `-D disc` (files at /cd root); filenames kept 8.3 (`fsmeta.txt`, not `wings64.meta`).
- Flycast 2.6 smoke: FAIL before any guest code runs — "Verify Failed: &mem_b[0] == sq_buffer + … driver.cpp:347"
  with dynarec on or off, and identically for tuxracer-dreamcast's TuxRacer.cdi. Host emulator issue, not the image.
- Awaiting: maintainer burns build/wings64.cdi and reports the M0 screen.

## 2026-10-10 — planning
No builds. Facts gathered from reference repos; see `docs/reference/`.
