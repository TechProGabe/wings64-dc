# wings64-dc — plan

Native Sega Dreamcast port of **Pilotwings 64** (N64, 1996), built from the
`gcsmith/Pilotwings64Decomp` matching decompilation on KallistiOS, with
**sh4zam** as the mandatory math layer. Deliverable: `./run.sh disc` produces a
**ready-to-burn `.cdi`** that boots on a retail Dreamcast from CD-R.

This file is the plan for an executing LLM. It decides architecture, orders the
work into milestones with exit criteria, and names the hard problems. Facts it
relies on live in `docs/reference/` (file:line cited); it does not repeat them.
Read `CLAUDE.md` first (hard rules), then this, then `kb/RESUME.md`.

---

## 0. Ground truth (verified 2026-10-10)

| fact | value | source |
|---|---|---|
| Decomp | `gcsmith/Pilotwings64Decomp` @ `1a8558f` (2026-09-28), MIT | `docs/reference/decomp-portability.md` |
| Match state | US: **100.00% code**, zero game-logic asm | decomp.dev; brief §2 |
| ROM | US `NPWE`, 8 MiB, SHA-1 `ec771aedf54ee1b214c25404fb4ec51cfd43191a` | `docs/rom-provenance.md` |
| Microcode | stock **Fast3D** (`graphics.c:606-607`), `G_TRI1` only, 32 opcodes, 13 combiners, always 2-cycle | decomp brief §3; Birdman64 brief |
| Audio | stock libultra alSyn/aspMain, 22050 Hz, 32 PVoices, all waves VADPCM@22050, 57 music + 107 SFX waves, 31 sequences | Birdman64 brief |
| Video | 320x240 16-bit, Z-buffer, LAN1, no Expansion Pak, 4 MB fixed map | decomp brief §5 |
| Per-frame load (flight) | 2.3–3.8k tris, 8–13k Gfx commands, 63–312 draws, ~200 texture loads | Birdman64 `renderer.md:118,164` |
| Textures | RGBA16/I4/IA8/I8/IA4/IA16 (+RGBA32 in UVBT), **no CI/TLUT**, ≤0x1000 B, TMEM-swizzled rows, up to 6 mips | Birdman64 brief |
| Assets | IFF `FORM` filesystem, 1272 files, index at ROM 0xDE720, MIO0 "GZIP" blocks; all UVxx formats fully documented by Birdman64 + decomp parsers in `texture.c` | decomp brief §4 |
| Cart I/O choke point | `_uvMediaCopy` (`memory.c:136`) + `_uvDMA` (`system.c:419-438`) | decomp brief §5 |
| Prior art | ssb64-dc (structure, discipline), Birdman64 (this game's patch set + libultra surface), sf64-dc/sm64-dc (Fast3D→PVR interpreter, AICA driver) | `docs/reference/*` |
| Host | macOS arm64, Docker CLI + colima, Flycast.app, no KOS installed | session check |

---

## 1. Architecture decisions (settled — see `kb/closed.md` for rejected options)

### D1. Layering
```
decomp (fetched, patched, read-only)   src/app, src/kernel, src/libultra/{audio,sp,gu}
         │  compiled unmodified except patches/ (endian, bitfield, UB)
         ▼
src/dc/decomp/   shadow headers (PR/os.h, PR/ultratypes.h, macros.h, segment_symbols.h)
src/dc/          the port: ultra/ (OS shim), gfx/ (Fast3D→PVR), audio/ (alSyn→AICA),
                 rom/ (ROM-in-RAM media), input/, save/ (EEPROM→VMU), db.h knobs
tools/           host tools: rom slicing, VADPCM→Yamaha ADPCM, checks, disc layout
scripts/         container, build, disc, hardware push, tests
```
Every departure from the decomp's behaviour is marked `DIVERGES: <reason>` in the
port source (ssb64-dc discipline). Decomp source is never edited in place; it is
patched by `patches/*.patch` applied at fetch time (one patch per reason, each
guarded with `#ifdef WINGS64_DC` where a guard is possible so the N64 build stays
byte-identical).

### D2. Memory model: **ROM-in-RAM, no streaming**
The ROM's data region is small enough to live in Dreamcast RAM. At boot, load
`disc/wings64.fs` (ROM bytes `0xDE720`–`0x6314D0`: filetable + filesys +
audio_seq + audio_ctl, ≈5.5 MB) into a 32-byte-aligned buffer. `_uvMediaCopy`
and `_uvDMA` become `shz_memcpy` from that buffer at `(rom_addr - 0xDE720)`.
`audio_tbl` (raw VADPCM, ≈1.9 MB) is **never** loaded to main RAM: it is
transcoded offline to a Yamaha ADPCM pool and uploaded to AICA RAM.

Budget (16 MB): KOS + code ≈1.5 MB · ROM fs image 5.5 MB · game arena 4 MB
(replaces the N64 map; framebuffers/Z live in VRAM, so the arena is actually
smaller) · gfx interpreter buffers + texture staging ≈1 MB · headroom ≈4 MB.
Boot cost: one ≈5.5 MB read, 6–11 s from CD-R behind a loading screen.
Per-level disc loading is a lever (`kb/levers.md`), not a requirement.

### D3. Endianness: **swap at the known parse points, keep assets byte-identical**
Assets stay big-endian on disc (an exact ROM slice). The game reads most fields
byte-wise (`uvConsumeBytes`/`uvMemRead`), which is endian-neutral. The bulk
copies (Vtx, Mtx u16 halves, Mtx4F, UVEN, UVTX Gfx words, task/spath/level
tables, fonts, audio banks) are swapped in place right after the copy, using a
C port of Birdman64's layout-validated `PW64_SWAP`. Birdman64's patch set is the
authoritative map of every site (`docs/reference/birdman64-sm64dc.md`). Bitfield
structs in `uv_filesystem.h` / `gbi.h` are reversed by patch (SysV LSB-first).

### D4. Rendering: **runtime Fast3D interpreter on raw PVR first; native bake second**
Phase A (M2–M3): a `gfx_retro_dc.c`/`gfx_pvr.c`-lineage interpreter rebuilt for
the F3D GBI, with every math call rewritten to current sh4zam names
(`docs/sh4zam-mandate.md` §N.3). Raw KOS `pvr_*` + store-queue direct render,
OP/PT/TR routing from render mode, PVR vertex fog, 640x480 RGB565 (480p on a VGA
cable), game coordinates 320x240 ×2. No GLdc.
Phase B (M6, kill-switched): PW64 builds model and contour display lists at load
time from a compact tri/vertex encoding (`texture.c:506-518, 663-671`). Hook
there to emit PVR-native vertex runs + precompiled poly headers per
UVMD/UVCT, transformed per object with register-resident XMTRX (ssb64-dc
`fighter.c` pattern). Interpreter stays for HUD, sprites, fonts, misc GBI
(253 raw macro uses), and as the fallback.

### D5. OS shim: **real KOS threads, libultra semantics preserved**
PW64 has few threads (kernel 0xC, render 0xFA, app 0xA, scheduler, audio, VI/PI
managers) and a message-driven scheduler (`sched.c`). Map `osCreateThread` →
`thd_create_ex` with priorities inverted into KOS order and distinct per thread;
`OSMesgQueue` → ring buffer + semaphore with real blocking; `osSetEventMesg(VI)`
posted from the KOS vblank handler; SP task runs synchronously on the render
thread (interpret DL → PVR lists) then posts SP/DP done; `osPiStartDma` =
`shz_memcpy` + post `OSIoMesg`; `osGetTime` = `timer_us_gettime64`; MIO0 in C.
If strict libultra priority semantics prove load-bearing (Birdman64 needed yield
patches in `title_screen.c`, `audio_seq.c`, `snap.c`), fall back to ssb64-dc's
coroutine shim (`sysshim.c`) — same API, deterministic switching.

### D6. Audio: **libultra sequence/sound players on SH4, synthesizer → AICA voices**
`src/libultra/audio` compiles (C). Keep `ALCSPlayer` (music) and `ALSndPlayer`
(SFX) running in a KOS audio thread woken by vblank with bounded catch-up.
Replace the synthesizer (`alSynNew`… voice calls) with an AICA voice driver in
the `aica_synth.c` lineage: one PVoice → one of 64 channels, `snd_sh4_to_aica`
command queue. Waves transcoded offline VADPCM → Yamaha ADPCM (loop points,
PCM fallback for >+1 octave pitch); whole pool resident in the 2 MB AICA RAM
(≈1.7 MB expected; measure — if it does not fit, keep SFX resident and page
music waves). Reverb: none at first, then AICA DSP program (ssb64-dc
`ssb_reverbexport.py` approach).

### D7. sh4zam everywhere
`docs/sh4zam-mandate.md` is binding: `-m4-single-only`, no doubles, XMTRX
discipline, no KOS `mat_*`, every `gu*`/libm call replaced per the table, memory
moves through `shz_memcpy*`/`shz_sq_memcpy32*`. Physics TUs (`hang_glider*`,
`gyrocopter`, `rocket_belt`, `cannonball`, `skydiving`, `birdman*`,
`jumble_hopper`, `camera`, `code_8170` collision) use the **exact** variants
(`shz_sqrtf`, `shz_invf`, `_safe`) not `_fsrra`, so replays (`fdr.c`) stay
deterministic; a purity check enforces no FSRRA/FSCA in those objects
(ssb64-dc `fpu_check.py`). Everywhere else, fast variants are the default.

### D8. Toolchain: **pinned container, also usable natively**
ssb64-dc container pattern (tested on macOS arm64). Pins in `docker/pins.env`:
KOS **v2.2.2** (or a pinned master commit), kos-chain **GCC 14.3.0** (sf64-dc
proven with sh4zam + N64 code; GCC 15.2 "stable" is an experiment, GCC 13.2
the fallback), **mkdcdisc** pinned commit (≥ v0.0.4), **sh4zam** = user fork
`TechProGabe/sh4zam` pinned commit (as a git submodule `external/sh4zam`),
decomp pinned `1a8558f` fetched into `external/pw64-decomp/` (gitignored) and
patched by `patches/`. `./run.sh setup|build|disc|release|test|run-hw`.

### D9. Disc: **mkdcdisc, one data track, CDI**
`mkdcdisc -e build/wings64.elf -d disc/ -n "Pilotwings 64" -a wings64-dc -o build/wings64.cdi`
(padding ON for CD-R; `-N` for emulator/ODE builds; `-L data-data` variant
offered for late VA2 consoles). `disc/` holds `wings64.fs`, `adpcm_pool.bin`,
`sample_table.bin`, `disc.id`, VMU icon. mkisofs+cdi4dc path kept as a fallback
knob (ssb64-dc `make_cdi.sh`). Nothing from the ROM is ever committed.

### D10. Testing ladder
Flycast locally for smoke + serial log; **real hardware is truth** (CD-R on the
user's MIL-CD console; dcload if a cable exists). Host build of decomp + port
with KOS stubs for logic tests; `fdr.c` flight-recorder replays and the ROM's
`demo.c` demos as determinism oracles; tree checks (ROM leak, VRAM fence,
DIVERGES format, sh4zam-name lint, purity). Pre-commit runs `./run.sh test --ci`.

---

## 2. Milestones (each ends with a bootable `.cdi`)

### M0 — Toolchain, skeleton, first disc  (exit: KOS hello boots from CD-R)
- `docker/` (Dockerfile, pins.env, fetch-src.sh, apply-patches.sh), `run.sh`,
  `scripts/{ctr,dc_env,build_target,make_cdi,run_hw,test_*}.sh` adapted from ssb64-dc.
- `external/sh4zam` submodule at the fork pin; `tools/rom/slice_fs.py` cuts
  `wings64.fs` from `baserom.z64` (SHA-1 checked) and refuses any other ROM.
- `src/dc/main.c`: KOS init, `vid_set_mode(DM_640x480, PM_RGB565)`, `pvr_init`
  (OPB 16/0/16/0/16, 512 KB vertex buffer), asset root probe (`/pc`, `/cd/disc.id`,
  `/rd`), load `wings64.fs`, print build stamp + fs file count via `uvMemInitBlockHdr`
  logic reimplemented in the port (validates ROM-in-RAM + endian reading).
- `./run.sh disc` → `build/wings64.cdi`. Smoke in Flycast; burn; boot.
- `db.h` knob facility (`DB_*`, off by default), `.cflags` stamp, `RELEASE=1`.

### M1 — The decomp compiles for SH-4 and for the host  (exit: link succeeds twice)
- Fetch + patch pipeline; port Birdman64's endian/bitfield/UB patches (drop
  64-bit, rate, widescreen ones) to `patches/`, each with a one-line reason file.
- Shadow headers `src/dc/decomp/` (`PR/os.h`, `PR/ultratypes.h`, `macros.h` with
  `#include_next` + `#undef __attribute__`, `segment_symbols.h`, libc shims).
- Defines: `-D_LANGUAGE_C -DVERSION_US -DBUILD_VERSION=VERSION_D -D_FINALROM
  -DNDEBUG -DTARGET_N64 -DNON_MATCHING -DAVOID_UB -DRECOMP_BUILD -DWINGS64_DC`.
  Flags: `-Os` default, `-funsigned-char -fwrapv -fno-strict-aliasing
  -ffp-contract=off -ftrivial-auto-var-init=zero`, `-m4-single-only -mfsrra -mfsca`,
  `-Wno-*` only on decomp TUs. Hot list (`-O3`): gfx core, `mtx.h` users, mixer.
- Compile `src/app`, `src/kernel`, `src/libultra/{audio,sp}` (no `gu` exists in this decomp); exclude
  `libultra/{os,io,libc,debug,vimodes}`, `rsp`. Stub every undefined `os*`/`al*`
  symbol from Birdman64 native-build.md §1 with `abort("TODO")`.
- Host build (`make -C src/dc/test hosttest`) with fake `kos.h`/`dc/pvr.h` and
  sh4zam `SHZ_BACKEND=SW` (links `source/sw/shz_xmtrx_sw.c`).
- Checks: `rom_leak_check`, sh4zam-name lint (greps for the non-existent names),
  DIVERGES-format lint, no `double` in TUs that include sh4zam (purity, phase 1).

### M2 — Boots to title and menus  (exit: title → file menu → level select on hardware)
- `src/dc/ultra/`: threads, mesg queues, events, timers, PI (ROM-in-RAM), VI
  (vblank), SP task dispatch, AI stubs, SI controller, EEPROM→VMU (0x208 bytes),
  MIO0 in C, `osMemSize`, `osTvType`, `osViModeTable`.
- Memory map: replace the 39 `0x80xxxxxx` literals (`memory.c:41-76`,
  `graphics.c:105-107`, `sched.c:219`, `system.c:223-227`, `audio_manager.c:40`)
  with an arena; framebuffer/Z-buffer pointers become PVR-side no-ops; keep
  `K0_TO_PHYS`/`OS_PHYSICAL_TO_K0` semantics (single OR on SH-4).
- `src/dc/gfx/`: Fast3D interpreter (F3D GBI, 16-vertex cache, `G_TRI1`), TMEM
  deswizzle in texture import, RGBA16/IA4→ARGB1555, I4/I8/IA8/IA16/RGBA32→ARGB4444,
  twiddled upload, 1024-entry address-keyed texture cache with reset on level
  load, six base poly headers bit-patched per draw, OP via DR, PT/TR buffered
  and flushed, near-plane clip, PVR vertex fog from `gSPFogPosition`, viewport
  via sh4zam screen matrix, `cimg==zimg` FILLRECT → Z clear no-op, TEXRECT/FILLRECT
  → sprites. All math per `sh4zam-mandate.md` §N.3.
- Input map (DC → N64): analog stick → stick (±80 scale), A→A, B→B, X→C-left,
  Y→C-right, D-pad → C-up/C-down + D-pad, L trigger → Z, R trigger → R, Start.
  Revisit per vehicle in M3.
- `DB_BOOT_LEVEL` knob to skip menus.

### M3 — One mission playable  (exit: Rocket Belt Beginner Class A playable to results, 30 fps min)
- Terrain (UVTR/UVCT), fog/env (UVEN), models (UVMD), LOD, collision, HUD, text.
- Endian sites outside the kernel: `task.c`, `spath.c`, `level.c`, `anim.c`,
  `user_paths.c`, `demo*.c`, `text_data.c`, `code_8170.c` tint loops, `code_A8C30.c`.
- Z mapping and `zclip` tuned for large outdoor far planes; decal bias; env-map
  `G_TEXTURE_GEN` via object-space lookat vectors + FIPR.
- Shadow volumes (`cimg==zimg`) → render as PT with depth-only or drop (DIVERGES).
- `uvCopyFrameBuf` (snap photos) and snow CPU framebuffer writes → PVR texture
  readback or software path; documented DIVERGES if deferred to M5.
- Hardware instrumentation (decisions 8, 9): SD card log with receipt, crash/assert hook, FPS
  monitor, title-screen settings menu to switch them.
- Determinism oracle: play the ROM's demo replays through `fdr.c` on host and on
  target; compare recorded state hashes per frame (`DB_FDR_HASH`).

### M4 — Audio  (exit: music + SFX in menus and flight, no dropouts over 10 min)
- `tools/audio/`: parse `audio_ctl` (ALBankFile, field-wise, Birdman64
  `pw64_audio_load.c`), extract all 164 waves from `audio_tbl`, VADPCM decode
  (decomp codec / sm64-dc `transcode.py` lineage), Yamaha ADPCM encode with loop
  carry and PCM fallback; emit `adpcm_pool.bin` + `sample_table.bin`; size check
  against 2 MB minus ring buffers.
- `src/dc/audio/`: alSyn voice API → AICA channels; sequence + sound players on
  the audio thread; `osAiSetFrequency(22050)` honoured by channel pitch; volume/pan
  mapping; `fix_aica_freq_gap`; long-sample streaming only if any wave exceeds
  65534 samples (check first).
- Reverb: off (DIVERGES) in M4; AICA DSP program in M5 if cheap.

### M5 — Full game parity  (exit: every vehicle/class/level completable; Birdman, Jumble Hopper, Cannonball, Skydiving; snap album; replays; credits; saves persist on VMU)
- Burn-down of visual parity: combiner coverage (13 modes), alpha compare, fog
  color, dither emulation (optional), coverage-AA edges (skip), canopy fringes.
- VMU: save file with icon; "SAVING…" notice; handle missing/full VMU.
- Pause/controller-disconnect behaviour; 4:3 only (no widescreen).

### M6 — Performance  (exit: ≥30 fps locked everywhere, 60 fps in menus; measured on hardware)
- Profile with `DB_PERF` (us per phase: interpret, transform, submit, PVR render).
- Native bake for UVCT terrain and UVMD models at load (D4 phase B), behind
  `WINGS64_NATIVE_GEOM` (default on only after a screenshot pair + hardware
  measurement; kill switch stays).
- I-cache: gfx hot path in `.text.hot.gfx` (<8 KB); `-O3` hot list by measurement;
  `-ffast-math` per TU where purity allows.
- Texture: optional VQ for the largest textures, 2 KB-aligned VRAM allocator if
  churn appears. Store-queue header submission (`shz_sq_memcpy32_1`).

### M7 — Release  (exit: `./run.sh release` → `wings64-release.cdi`; README; NOTICE)
- Strip `db.o`, `-DNDEBUG`, release IP.BIN fields, `-L data-data` variant.
- README (build from your own ROM), NOTICE (MIT: decomp, Birdman64-derived
  patches, ssb64-dc-derived scripts; BSD: gfx core lineage; CC0: sf64-dc pieces),
  `PROVENANCE.tsv`. Tag `v0.1.0`. Cut releases locally, never in CI.

---

## 3. The hard problems

| # | problem | why hard | plan |
|---|---|---|---|
| H1 | Endianness of bulk-copied ROM structs and MIPS-order bitfields | silent corruption, ~100 sites | Birdman64's patch map + layout-validated `PW64_SWAP`; host test compares parsed structs against Birdman64's Rust parsers' documented layouts |
| H2 | Fast3D → PVR fidelity: 2-cycle combiners, `G_RM_ZB_XLU_SURF2`, fog-shade pass, texgen env-map, `cimg==zimg` tricks | PVR has one texture unit, no combiner | per-vertex combiner evaluation into argb/oargb (sm64-dc), TEXEL1=TEXEL0, offset color for fog/env; enumerate all 13 combiners with a check that every one seen at runtime is handled |
| H3 | alSyn → AICA with 32 voices at 22050 Hz, ADPCM pitch limits, 2 MB pool | timing + memory | offline transcode with PCM fallback; measure pool size early (M1 tool); stream only if needed |
| H4 | libultra scheduler semantics on KOS threads | priority-preemptive, no time slice on N64; KOS slices | distinct priorities, `thd_set_hz` low, yield patches; coroutine shim fallback |
| H5 | IDO-era UB and the fixed 4 MB KSEG0 map | crashes that IDO hid | Birdman64 UB patch list; arena replaces literals; `-ftrivial-auto-var-init=zero`; `AVOID_UB` |
| H6 | Float determinism vs sh4zam approximations | flight model + replays | exact variants in physics TUs, purity check, replay hash oracle |
| H7 | Load time and RAM fit of ROM-in-RAM | 5.5 MB boot read, 16 MB total | loading screen; measured budget in `kb/STATE.md`; per-level loading as lever |

## 4. Risk register

| risk | likelihood | impact | mitigation |
|---|---|---|---|
| GCC 14.3 miscompiles a decomp TU with `-Os`/LTO | medium | medium | per-TU `-O0` quarantine list with evidence (OpenCrossing pattern), no LTO until M6 |
| AICA pool > 2 MB | medium | high | measure in M1; PCM8 fallback; page music waves |
| Audio thread starves renderer (or vice versa) on real hardware | medium | high | priorities, bounded catch-up, hardware soak test (`DB_SOAK`) |
| Flycast hides cache/timing bugs | high | medium | hardware is truth; burn every milestone |
| sh4zam API drift between fork and upstream | low | low | pinned submodule; name lint against `dist/api/index.json` |
| Decomp upstream renames break patches | medium | low | pinned rev; `./run.sh decomp-refresh` only deliberately |
| mkdcdisc image rejected by some consoles | low | medium | `-L data-data` variant; cdi4dc fallback |
| Snap photo framebuffer readback unsupported | medium | low | PVR render-to-texture or DIVERGES |

## 5. Maintainer decisions (answered 2026-10-10)
1. **Hardware path: CD-R only.** No dcload cable. An SD card adapter is available
   for saving logs (see 8): the SD log writes the serial log to `/sd/wings64/log-NNN.txt`
   (kos `-lkosfat`), synced every 2 s; on-screen overlay for `DB_PERF` numbers.
   `run-hw` is therefore "burn this file". Every milestone burns one disc.
2. **Video: composite/S-video only (480i NTSC).** Render 640x480 interlaced; 480p
   path stays in code (KOS picks it on VGA) but is untested until a VGA box exists.
3. **Boot load: ROM-in-RAM accepted** (6–11 s behind a loading screen with progress).
4. **GCC pin: 14.3.0.** Fallback 13.2.0; 15.2 only as a measured experiment.
5. **v0.1 frame rate: locked 30 fps** (N64 parity). 60 fps in flight is the M6 stretch.
6. Console revision unknown → ship both default and `-L data-data` images in M7.
7. **(2026-10-10, supersedes 1 for gating) No CD-R burns.** Every milestone exit is judged in
   Flycast (`./run.sh smoke`: pinned v2.7 built from source in `docker/emu/`, Xvfb, HLE BIOS,
   composite cable) via serial marker + screenshots. Performance numbers from it are labelled
   "emulator" and are not hardware evidence. CD-R images are still produced by `./run.sh disc`.

8. **(2026-10-11) Hardware test logging = OpenCrossing-Dreamcast's proven SD path.** Every dev
   build carries the SD log (`src/dc/db.c`); `DB_LOG_SD=1` turns it on at boot, the title-screen
   settings menu (9) switches it at runtime. Rules carried over from OpenCrossing's hardware runs
   (`kb/traps.md`): serial-port adapter, SCIF probe only (SCI waits forever with no card); the
   card owns the serial port, so the console goes silent while it is up; the card comes up after
   the game loop runs, never at `main()`; one file per boot opened `O_TRUNC` and held open (never
   `O_APPEND` on fs_fat); `fs_fat_sync` after every write plus a `receipt.txt`; MBR FAT32 with
   4 KB clusters. A CPU-exception/assert hook (`DB_CRASH`, on in dev builds) writes the faulting
   thread, PC, registers and stack words to the log and flushes the card. SD images are judged on
   hardware only (Flycast has no adapter); the crash hook is exercised in Flycast with
   `DB_CRASH_TEST`. Procedure: `docs/testing.md` "SD card log".
9. **(2026-10-11) Title-screen settings menu** in the game's own look (fonts, boxes, colours,
   cursor, sounds of the title's Start game / Option menu): FPS monitor on/off, SD card logging
   on/off with its status. Settings persist with the save (VMU). Port feature, not a game change;
   marked `DIVERGES: port settings menu`.

## 6. Out of scope
Widescreen, JP/EU versions, mods, 32 MB RAM mod, emulator-specific fixes,
upstream decomp PRs (its CONTRIBUTING bans LLM contributions; we consume MIT).
