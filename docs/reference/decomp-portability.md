# Pilotwings64Decomp: portability brief

Source: gcsmith/Pilotwings64Decomp @ `1a8558f` (2026-09-28, "Cleanup (J) config
symbols files (#178)"). MIT (© 2026 Garrett Smith). Paths relative to that repo.
Submodules (`tools/ultralib`, `tools/Torch`, `tools/ido-static-recomp`,
`tools/asm-processor`, `tools/asm-differ`, `tools/m2c`, `tools/decomp-permuter`)
are NOT needed by this port. `tools/splat_ext` referenced at
`config/us/pilotwings64.us.yaml:52` does not exist.

**Headline:** the US build has **zero C functions left in assembly**. Only two
`GLOBAL_ASM` stubs remain (`src/app/control_info.c:420`, `:467`), both inside
`#if defined(VERSION_JP)`.

## 1. Build (upstream)
- `make dependencies` (Makefile L238-240): n64crc + ido-static-recomp IDO 5.3 + pip.
  `make extract` (L233-236): `python -m splat split config/us/pilotwings64.us.yaml`.
  `make init` (L228-231): clean, extract, `make -j`. Default `all` → `verify` (L217-223).
- Split: `config/us/pilotwings64.us.yaml` (2103 lines, `compiler: IDO`, `gfx_ucode: f3d`).
  261 `c`, 20 `hasm`, 4 `textbin` + 3 `databin` (RSP ucode). Live asset segments:
  `filetable` 0xDE720, `filesys` 0xDF5B0, `audio_seq` 0x618B70, `audio_ctl` 0x62D460,
  `audio_tbl` 0x6314D0 (L754-772). JP config in `config/jp/`.
- Compiler: `CC := tools/ido-static-recomp/build/5.3/out/cc` (L65), `-O2` (L113),
  `-mips2 -32` (L119), `-g0` (L145). Per-file (L175-209): libultra audio and `sp/sprite`
  at `-O3`; libultra `os/`, `io/`, `debug/` at `-O1`; `ll.o` `-mips3`; `kernel/debug.o`
  `-O1 -g -mips1`. Defines (L130-137): `-D_FINALROM -DWIN32 -DNDEBUG -DTARGET_N64
  -DBUILD_VERSION=VERSION_D` (libultra 2.0D headers; flib docs say 2.0I).
- **No NON_MATCHING / MODERN / SHIFT / AVOID_UB target.** `include/macros.h:8-12`
  mentions NON_MATCHING/AVOID_UB but the `#error` is commented out. `RECOMP_BUILD`
  (Makefile L23, L139-141; `macros.h:57-63`) only drops `static`.
- **Every C file is already syntax-checked with clang/gcc** `-fsyntax-only -std=gnu90
  -m32 -Wall -Wextra` (L156-157, L277). `__GNUC__` guards exist in `macros.h:21-54`.
- libultra: in-repo C in `src/libultra/` (125 `.c`: audio 59, io 44, os 19, sp 5,
  libc 2, vimodes 2, debug 1). `tools/ultralib` unused. asm-only pieces are splat
  `hasm` blobs (bcopy, bzero, sqrtf, cache ops, exceptasm, interrupt, getcount, setsr,
  …, yaml L151-255). **`kernel/decompress_mio0` is hasm** (yaml L114; called from
  `src/kernel/filesystem.c:81`). `entrypoint` is hasm (L71).

## 2. Coverage
265 `.c`, 4 `.s` (all `src/rsp`, `.incbin` wrappers).

| dir | .c | lines | C fn defs (approx) | address-only names | GLOBAL_ASM |
|---|---|---|---|---|---|
| src/app | 105 | 50,241 | ~1193 | ~625 | 2 (JP only) |
| src/kernel | 35 | 17,159 | ~477 | ~156 | 0 |
| src/libultra | 125 | 14,631 | ~237 | 0 | 0 |
| src/rsp | 0 (4 .s) | — | — | — | binary blobs |

No game logic in asm. Cost is readability: ~half of app functions address-named,
~20k `unkXX` field references. No progress doc in `docs/`; progress via decomp.dev
badges and `tools/discord_progress.py` (CI).

## 3. Structure
- **src/app (game):** vehicles `hang_glider0/1/2.c`, `rocket_belt.c`, `gyrocopter.c`
  (1958 lines), `cannonball.c`, `skydiving.c`, `birdman0/1/2.c`, `jumble_hopper.c`,
  per-vehicle `*_sound.c`; `code_7CF30.c` gyrocopter, `code_AC1A0.c` rocket-belt
  support. Tasks/objects: `task.c`, `rings.c`, `balls.c`, `targets.c`, `pads.c`,
  `hover_pads.c`, `thermals.c`, `wind.c`, `bonus.c`. World props: `boats.c`,
  `ferry.c`, `whale*.c`, `shuttle.c`, `ski_lift.c`, `fountain.c`, `smoke.c`, `mist.c`,
  `snow.c`. Presentation: `camera.c`, `hud.c` (2077), `snap.c` (2201), `map3d.c`,
  `title_screen.c`, `file_menu.c`, `level_select.c`, `pilot_select.c`, `results.c`,
  `replay_screen.c`, `credits.c`. Other: `fdr.c` (flight recorder/replay), `demo.c`,
  `save.c`, `environment.c` (fog + terrain palettes), `level.c`. `code_82B90.c`
  (2364) unnamed, collision.
- **src/kernel (Paradigm "UV" engine):** `graphics.c`, `geometry.c` (immediate-mode
  `uvVtxBeginPoly/uvVtx`), `texture.c` (ALL UV parsers), `terrain.c`, `env.c`,
  `dobj.c`/`sobj.c` (dynamic/static objects with LOD), `chan.c`; services `sched.c`,
  `system.c` (threads, DMA, EEPROM, controllers), `memory.c`, `filesystem.c`,
  `font.c`, `sprite.c`, `anim.c`, `fx.c`; audio `audio_manager.c`, `audio_seq.c`,
  `audio_emitter.c`; `code_8170.c` (3445, largest) = collision/terra query
  (`uvTerraGetBox/Pt/Seg/Plane`, `_uvDbSortHits`).
- **src/rsp:** stock binaries `rspboot.s`, `f3dex.s` (gspF3DEX_fifo), `fast3d.s`
  (gspFast3D), `aspMain.s`. `src/kernel/graphics.c:596-597` assigns F3DEX_fifo then
  **`:606-607` overwrites with gspFast3D → the game uses Fast3D.** `F3DEX_GBI` not
  defined; FIXMEs "older f3d ucode" at `chan.c:22`, `dobj.c:21`, `geometry.c:6`.
- **Audio:** stock libultra alSyn + aspMain. `audio_manager.c`: `alInit` L137,
  `alCSPNew`, `alBnkfNew` L387, `maxPVoices = 32` L65, `osAiSetFrequency(22050)`
  L126 (`OUTPUT_RATE`, `include/kernel/uv_audio.h:14`). Audio thread L161; task built
  L225-229; ROM-streamed sample DMA via `osPiStartDma` L302, `DMA_BUF_SIZE` 0x800.

## 4. Assets
- Container: IFF-like ROM filesystem. `docs/pilotwings64_filesystem.md` (1019 lines),
  `docs/pilotwings64_imhex.hexpat`. `UVRM`/`TABL` (MIO0) = index of (tag,size).
  Each file: `FORM`, length, tag, sub-chunks (`COMM`, `PAD`). Index walked by
  `uvMemInitBlockHdr` (`src/kernel/texture.c:61-121`); `uvMemLoadDS` (L135)
  dispatches by tag.
- Parsers (`texture.c`) + structs (`include/kernel/uv_graphics.h`):
  UVMD `_uvParseUVMD` L444, struct L218-230, LOD/part tables L199-216;
  UVCT contour L607, struct L103-118; UVTX `_uvExpandTexture` L707, struct L242-262;
  UVTR L969, struct L135-143 (tiles = matrix + UVCT ptr); UVBT L1002, struct ~L339;
  UVEN L398, struct L150-172; UVSQ L420, struct L274-280; UVLV `_uvExpandTextureCpy`
  L843, struct L341-362; UVTP L951; UVTI L821; UVLT L391 (unpopulated); UVFT struct
  L311-318; UVAN struct L289-303 (`uvJanimLoad`); UVSY system data; UVSX sound bank
  (CTL/TBL). Game-side: `ADAT` text, `UPWL` level objects, `UPWT` task
  (`task.c:613`), `3VUE` paths (`user_paths.c:62`).
- Extraction: splat dumps opaque `.bin` blobs into `bin/` and links them back
  (Makefile L299-301). No per-asset extraction, no Torch config.
- **Port-critical:** assets read big-endian via `uvConsumeBytes`/`uvMemRead`
  (`texture.c:370`, `memory.c:185`) — endian-neutral — BUT whole arrays bulk-copied raw:
  `Vtx` tables (`texture.c:479-480`, 633-634), `Mtx4F` tables, raw N64 `Gfx`
  texture-load display lists in UVTX (L760-762) patched in place at `G_SETTIMG`
  (L803-815). Model and contour display lists are BUILT AT LOAD from a compact
  tri/vertex encoding (L506-518, 663-671) → hook for emitting PVR-native geometry.

## 5. Memory / hardware
- 4 MB, no Expansion Pak. Hardcoded map (`memory.c:42-53`): framebuffers 0x800DA800
  and 0x80100000 (0x25800 each = 320x240x16), Z-buffer 0x803DA800-0x80400000, heap
  0x8004181C (`memory.c:341`) to `osMemSize + 0x80000000` (`:342`); gfx setup
  `graphics.c:105-107`. 41 literal 0x80xxxxxx addresses in app/kernel.
- Video: `SCREEN_WIDTH 320`/`SCREEN_HEIGHT 240` (`uv_graphics.h:10-11`),
  `OS_VI_NTSC_LAN1`/`PAL_LAN1` (`system.c:192-211`), Z-buffer on
  (`graphics.c:175-176`). DLs double-buffered `Gfx gGfxDisplayListBase[2][4200]`
  (`graphics.c:31`), overflow check L582.
- Threads: render (`system.c:320`, pri 0xFA), kernel (`:245`, 0xC), app (`:323`,
  0xA), scheduler (`sched.c:226`), audio (`audio_manager.c:161`), libultra VI and PI
  managers (`sched.c:217`, `system.c:318`).
- DMA: synchronous `_uvDMA` = `osPiStartDma` + blocking wait (`system.c:419-438`).
  `_uvMediaCopy` (`memory.c:136`) chunks at 4 KB; address bit 31 set → RAM, else ROM.
  **Single clean choke point** for replacing cart reads.
- Save: EEPROM `osEepromProbe/LongRead/LongWrite` (`system.c:89,115,163`), 0x208-byte
  limit (`system.c:84`), used by `src/app/save.c`.
- Controllers: `osContInit` (`system.c:343`). No Rumble Pak / Controller Pak code.

## 6. Terrain / rendering
- UVTR grid of tiles (4x4 matrix + UVCT mesh), drawn by `_uvTerraDraw`
  (`src/kernel/terrain.c`), collision via `code_8170.c`.
- Sky/fog: `src/kernel/env.c` (`uvEnvProps`, fog factor, `_uvEnvDraw`),
  `src/app/environment.c` (fog RGB, `envLoadTerrainPal`). LOD: per-model
  `lodTable`/`lodRadius` in `dobj.c`/`sobj.c`.
- No custom RSP tasks. One gfx task (`graphics.c:571-607`), one audio task, via
  `osSpTaskLoad/StartGo` (`sched.c:139-164`).
- **GBI usage small:** 253 gSP/gDP macro uses in app+kernel (gsSP1Triangle 30,
  gDPPipeSync 27, gSP1Triangle 23, gSPVertex 14). Most rendering goes through the
  kernel `uvGfx*`/`uvVtx*` wrapper API (28 app files use it) → compact surface.
- RDP modes (web brief): heavy 2-cycle (`G_CYC_2CYCLE` in init DL and
  `uvGfxStateDraw`); `G_RM_PASS`, `G_RM_ZB_XLU_SURF2`, `G_RM_OPA_SURF/_SURF2`;
  `G_ZBUFFER|G_SHADE|G_CULL_BACK|G_SHADING_SMOOTH`; fog
  `gSPFogPosition(factor*1000,1000)` via `uvGfxSetFogFactor`;
  `G_TEXTURE_GEN|G_LIGHTING` env-map path; `G_TL_TILE` LOD.

## 7. License / policy
MIT. **CONTRIBUTING.md bans LLM-generated contributions upstream** → this port never
sends PRs upstream; it consumes the decomp at a pinned revision under MIT and keeps
its own patches. CI: regress-internal/external/validate + format. Discord
https://discord.gg/bkrYXSjrKE.

## Verdict
Best-case N64 decomp base: no game-logic asm, GCC/clang-clean syntax, 32-bit
pointers, narrow render wrapper, stock Fast3D, stock alSyn. Blocking gaps:
1. No modern-compiler build: write an SH-4 GCC build; drop splat linking, IDO flags.
2. Replace libultra: ~20 hasm routines (MIO0 trivial in C), threads/mesg queues,
   VI/PI/SI, EEPROM. `src/libultra` C is N64-hardware code (keep `gu/`, audio seq).
3. Fast3D DLs + RDP state + alSyn/aspMain → PVR + AICA.
4. Big-endian assumptions in the asset path: bulk-copied `Vtx`/`Mtx4F`/`Gfx`,
   MIPS-layout bitfields in `uv_filesystem.h`/`uv_graphics.h`. Swap on load.
5. Hardcoded KSEG0 addresses, fixed 4 MB map, `OS_PHYSICAL_TO_K0` tricks.
6. IDO-tolerated UB: `va_start` on promoted types (`chan.c:360`,
   `audio_emitter.c:174`, `code_1050.c:43`), deliberate fault `*(u16*)1 = 0`
   (`memory.c` `_uvMediaCopy`), possibly-unset index (`fdr.c:181`), 15 `@fake` hacks.
