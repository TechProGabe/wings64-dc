# state-log — evidence, newest first

## 2026-10-11 — M3 done (emulator), settings menu, perf
- Combiner compile + fit cached per (cc0, cc1, cycle, fog, prim, env): fit 15 -> 0.4 ms per flight frame;
  per-triangle state compare skipped via gpvr_state_serial; u/v scale folded per state. Flight ~21 ->
  ~30 fps in the air, ~20 on the launch pad (emulator). Kill switches WINGS64_CC_CACHE, WINGS64_STATE_SKIP.
- Gate made robust: presses last >= 3 controller polls and steps are state-relative (s<state>+ms), re-armed
  after load gaps; wall-clock scripts broke whenever load times shifted. Gate: flight 89 s -> results 110 s.
- Title "Settings" menu (Opus agent, worktree merge 191e8b5): game's own Option-screen code, FPS monitor
  in the HUD style, SD card log toggle with status; VMU persistence. Reviewed: screenshots match the
  Option screen's look; tests, purity and gate pass on merged main.
- Flycast: SD log ON stalls the main monitor thread in the SCIF probe and emu_smoke then hung on its
  recorder; recorder now bounded. Audio still silent (M4).

## 2026-10-11 — M3 gate PASS (emulator): terrain, sky, SD log
- Island terrain was never drawn: the camera struct declares the ground view polygon `Vec3F unk328[1]`;
  GCC 14 cut the loops that write/read its 5 points to one, so tile sub-quad masks came out empty.
  patches/020 sizes unk250/unk2E0/unk328 to 6 (offsets asserted); decomp TUs add
  `-fno-aggressive-loop-optimizations`. Terrain, lighthouse, castle, trees now render in flight.
- Black band under the sky: the cloud strip (PT, combiner alpha 1) used texture alpha. Texture alpha
  now follows the combiner's alpha texel term.
- Title screen compared with the maintainer's N64 photo: logo, menu text and pilots match within the
  photo's overscan crop (~6-9 %).
- Gate: the stick step at 36 s fell inside a 6 s controller-poll gap after the menu change (the run
  flew the hang glider); moved to 37 s. Rocket Belt Beginner: test update 65.3 s -> results 85.5 s,
  `WINGS64_M3_OK`. Flight ~21 fps (emulator) -> the M3 "30 fps min" is open (M6 work or earlier).
- SD card log rebuilt on OpenCrossing-Dreamcast's hardware-proven path (SCIF probe, console muted while
  the card owns the serial port, armed 6 s into the game, O_TRUNC + held fd, fs_fat_sync every 2 s,
  receipt.txt), switchable at runtime. Crash/assert hook verified in Flycast with DB_CRASH_TEST
  (illegal instruction on the main thread: report logged, thread parked, game threads kept running).
  SD path itself is unverified until a hardware run.

## 2026-10-11 — M2 gate PASS (emulator), build ee58e1dd-1011-1259
- `./run.sh gate` (DB_AUTOPLAY script 8000:S,16000:A,30000:A): state title (500 ms) -> file menu
  (17500 ms, gfx 231) -> vehicle & class select (30250 ms, gfx 488) -> WINGS64_M2_OK. Video:
  build/evidence/m2-gate-emu.mp4 (untracked: renders ROM assets).
- OS shim on KOS threads (os_shim.c + kos_bridge), patches 017 (RDRAM arena via N64_K0) and 018.
  Fix on the way: osPiReadIo must copy the ROM word raw (callers read it as bytes; wrong order broke the
  audio seq header read).
- Fast3D interpreter (gfx_f3d.c + gfx_pvr.c): title, menus, fonts, sprites render. Profiling (emulator):
  per-vertex combiner evaluation was 22-50 ms/frame -> per-state linear fit -> ~40 ms total DL time.
- Fonts were invisible: IA4 font atlases are 432 texels wide and tiles declare more rows than TMEM
  holds; converter now allows 1024 wide and clamps to loaded rows.
- This game's older F3D numbers RDPHALF_1/2 as 0xB3/0xB2 (geometry.c, sprite.c): texrect halves are
  taken by position.
- Agents: texture converter, VMU save, F3D GBI header (908 checks), audio pool probe, tree checks.

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
