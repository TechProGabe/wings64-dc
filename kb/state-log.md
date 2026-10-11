# state-log — evidence, newest first

## 2026-10-10 — M1 gate PASS (emulator), build 3b9d3a8d-1011-0200
- Patches: Birdman64's 76 per-file patches (decomp 2529dab) applied by exact context (their @@ counts are
  off), 292 hunks sorted by reason. Kept 16 patches (bitfield 3, UB 8 incl. 3 own, yield 1, endian 4),
  every change under `#ifdef WINGS64_DC` / `#else` original. Dropped 64-bit, frame-rate, widescreen,
  HLE fb hooks, logging, present tick.
- SH-4: 197 decomp TUs compile clean (GCC 14 int-conversion errors relaxed for decomp only: ILP32);
  link resolves the game from `bootproc` with 41 TODO libultra stubs + data stubs + segment constants.
  Layout static asserts pass (Gfx 8, Vtx 16, Mtx 64, UnkCommStruct 0x18, photo record 0x18, Bitmap 16).
- Host: armhf (ILP32, LE) + qemu-arm: decomp + port + sh4zam SW link; swap/xmtrx unit tests and the
  host M0 check (crc, 1272 FORMs, walk end 618B6C) pass.
- Purity: GCC asm of 202 decomp-flag TUs has no fsrra/fsca/fmac (objdump showed 13 false hits: literal pools).
- Audio probe: ADPCM pool 2,009,944 B vs 1,835,008 B budget → does not fit resident; 6 waves > 65534 samples.
- Emulator: WINGS64_M1_OK; screen "decomp 1a8558f linked, bootproc @ 0x8c0842c4", RAM image 1823 KB,
  heap 6576 KB, free 7984 KB.

## 2026-10-10 — M0 gate PASS (emulator), build 574fd2b-1011-0141
- Maintainer decision: no CD-R burns; milestones gate on the emulator.
- Flycast v2.7 (5aa091f) built from source in docker/emu (Debian arm64, Xvfb, llvmpipe), HLE BIOS,
  composite cable. Serial: assetroot /cd; romfs 5582256 bytes in 2881 ms; crc32 DAFCC147;
  uv_files 1272 form_ok 1272 walk_end 618B6C; WINGS64_M0_OK.
- Screenshot (+5 s): every line green, "M0 RESULT: PASS", alive counter 7 s. heap used 6565 KB.

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

## 2026-10-10 — planning
No builds. Facts gathered from reference repos; see `docs/reference/`.
