# state-log — evidence, newest first

## 2026-10-11 — hardware run 4 (disc 123d29e-1012-0123: shadows, draw order, splash), maintainer report
- No SD card in this run, so no log. Maintainer: everything else looks right now; two issues left:
  water looks wrong (looked right on 462f919, so a regression from a84f8b9) and occasional pop-in on fast
  left/right turns. Both queued in kb/STATE.md with suspects and kill switches to bisect.

## 2026-10-11 — frame-rate pass (emulator): ~55-57 fps everywhere, root cause in the scheduler
- Fable consult -> docs/fps-plan.md; executed changes 1-6 plus decomp -O2 and fsqrt (table in kb/STATE.md),
  each with a kill switch and a gate pass. Flycast DL went title 28.7 -> 19.5 ms, menus 28.3 -> 21.3,
  flight 20.6 -> 16.5 before the root cause below.
- New instruments: per-thread CPU split (`cpu:` lines, KOS thd_get_cpu_time), DL wall vs CPU per frame
  (`dl avg30 wall .. cpu ..`), an in-DL sampler (what runs while the interpreter is inside a display list),
  per-game-state profile windows with a serial fallback, `scripts/gfxstat.sh`.
- **Root cause**: DL wall 21.4 ms but interpreter CPU 12.1 ms; ~44% of every DL window ran the decomp's
  kernel thread `while (1) { }` idle spin. KOS v2.2.2 `thd_schedule` picks the next thread before
  re-queuing the running one on a timer tick, so any ready thread gets the CPU whatever its priority.
  patches/023 parks that thread on a message queue. Result: title 57, menus 54-56, flight 56, results 58 fps
  (was 22-33); DL wall == CPU (11.4 ms title, 9.9 flight); idle spin 62% -> 0%; the gate reaches flight at
  50 s instead of 108 s (menu loads were starved too). Game time unchanged (flight 19.75 s at 57 fps).
- fsqrt: the game's sqrtf was newlib's software loop; SH-4 fsqrt is bit-identical except denormals (FPSCR.DN
  flushes them, even in compares) which go to newlib: 199,122 values, 0 mismatches.
- The Rocket Belt column bug reproduces in Flycast (camera looking up during the climb).
- Hardware confirmation pending (no burn scheduled).

## 2026-10-11 — second hardware log (disc 0a43221-1011-2141, profiler build)
- LOG001.TXT (158 s, 0 dropped): no PROF dump (chord not registered or not pressed; auto dump is at 240 s).
  Per state, fps / DL median: title 20 / 32 ms, file menu 13 / 45, class select 14 / 43, pilot select
  31 / 10, test details 9 / 61, flight 25 / 25 (80 s of flight), results 34 / 18. Same picture as run 1.
- Maintainer: no further hardware test soon; work from these numbers.

## 2026-10-11 — SD log works on hardware (disc deb13bc-1011-2120, NAS wings64-dc-20261011b-sdfix)
- Maintainer: "Saving works confirmed", screen shows "SD LOG000 TXT": the backported KOS 1c6398f SD stack
  (src/dc/kos_sd) brings the card up where v2.2.2's stuck at "probing".
- **First hardware log** (LOG000.TXT, 260 s, 0 dropped, 0 errors; copy kept off-repo): PVR wait ~7 us and
  finish ~90 us in every state, so the GPU is never the limit; the SH-4 display-list interpreter is.
  DL ms median/max per state: title 30/52, file menu 43, class select 41, overview/details 27-29/57,
  flight 27/75, results 13, congratulations 69. FPS (gfx tasks per second): title ~20, menus ~18-19,
  flight ~23, results ~38. Loads: test setup -> flight 7 s with no frames. Audio: 22.3-22.5 kHz,
  0 misses, 13 DAC drops in 260 s, alAudioFrame ~80 us avg.

## 2026-10-11 — first hardware run: M4 hw-test disc (db2e587-1011-1821), maintainer report
- Boots and plays on a retail console (CD-R; composite). **Audio "sounds really good"** (music + SFX).
- Frame rate 15-20 fps (hardware; emulator had claimed ~30 in the air / ~20 on the pad).
- Menus very slow between loading screens (emulator: 5-7 s per menu change; same on hardware).
- Visual: a cylinder under the Rocket Belt pilot in flight; landing target texture "all striped";
  pilots do not appear on the pilot select screen.
- SD card log status read "probing" with the card in the adapter; the card (MBR FAT32, 4 KB clusters,
  OpenCrossing's GMON.1 on it) got no files. Fix for the next disc: OpenCrossing's KOS 1c6398f SD stack
  (sd.c + scif-spi.c, time-bounded waits) backported as src/dc/kos_sd, 8.3 upper-case file names, and the
  SD state drawn under the FPS counter. Flycast (no adapter): "SD FAIL INIT" within seconds, game runs.
- Maintainer: "visually it's pretty good"; FPS is the priority. Flycast had claimed ~30 fps in the air:
  emulator timing is not evidence (CLAUDE.md), now confirmed the hard way.

## 2026-10-11 — M4 done (emulator): music + SFX on the AICA
- Design consult first (Fable agent): docs/audio-design.md; amendments from execution at its end.
- `tools/audio/transcode.py` (vadpcm.py + yamaha.py): 164 waves -> disc/wings64.aud, 1,860,032 B. Looped
  waves truncated at loop end (libaudio never plays the tail), 5 over-length waves decimated to 11025 Hz,
  so the whole pool fits AICA sample RAM (1,900,544 B; 32 KiB headroom): no music paging. VADPCM decode
  bit-exact against every ALADPCMloop.state; Yamaha SNR median 24.5 dB, worst 10.3 dB (saw-like wave).
- `src/dc/audio`: alSyn voice API + alAudioFrame re-implemented on AICA channels 0-31 (stock KOS
  firmware, one command batch per audio frame); libaudio synthesizer/RSP filter TUs excluded from the
  build. Virtual AI DAC in os_shim.c paces the audio manager in wall-clock time with deficit carry.
- patches/022: the audio task completes at once; routed through the scheduler it waited behind CPU
  display-list work and the audio thread lost 7% of retraces (18% DAC drops, ~20.4 kHz). After: 300
  frames / 5 s, 22.0-22.3 kHz, 0 drops. Raising the audio thread priority (200) measured nothing; kept 110.
- Gate (`./run.sh gate`, timeout 150 -> 200 s): WINGS64_M3_OK + WINGS64_M4_OK. Music key-ons 2194 in
  menus / 462 in flight, SFX 25, AICA KEYONB set in both, 0 misses, 0 stall windows, alAudioFrame
  27-80 us/frame avg. Kill switch `-DWINGS64_AUDIO=0` builds and runs (silent M3 path).
- Title is ~14 s slower to the file menu under the same script: uvaSeqStop now waits ~0.9 s for note
  releases at music changes (real-time audio clock; M3's stub AI ran it 1.32x fast), and autoplay re-arms
  after >0.5 s poll gaps. Control run (no patch 022, silent) reproduced the old 21 s; silent + patch 022
  reproduced the new 35 s.
- Soak (`./run.sh soak`, attract mode, no input, 600 s wall clock): WINGS64_M4_SOAK_OK; 35,820 audio
  frames, music 11,687 / SFX 157 key-ons, 0 misses, 0 stall windows, 13 DAC drops (0.04%), 22.2 kHz.
  Flycast sound captured (EMU_AUDIO=1, SDL disk driver, 613 s): -15..-26 dBFS while playing, 0 clipped
  samples. Silences line up with the game: 7-11 s at every demo setup -> flight level load (driver shows
  0 voices requested) and ~1.5 s at title music changes; none while voices were keyed.
- Hardware staging (maintainer: "ship it with fps and logging enabled by default"): `./run.sh hwdisc`
  builds build/wings64.cdi + wings64-dd.cdi with WINGS64_FPS_DEFAULT=1 (FPS monitor on when the VMU holds
  no settings; checked in Flycast) and DB_LOG_SD=1 (SD log from boot; never in Flycast images). Copies go
  to the maintainer's NAS share (wings64-dc/). `./run.sh run-hw` checklist has the audio listening items.
  Untested on hardware: ADPCM loop latching, pool upload time on CD-R, loudness balance, SD log here.

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
