# ssb64-dc architecture brief (primary structural reference)

Source: beigeceleron/ssb64-dc (fully playable native DC port of Super Smash Bros.
from ssb-decomp-re). ~206k lines in `src/dc/` (302 files), 778 `DIVERGES` markers,
~1/3 of the game is decomp C compiled unmodified (171 Makefile rules point at
`$(SSB_DECOMP_DIR)/src/...`). Paths relative to that repo.

## 1. Repo / build shape
- `run.sh` dispatcher: `cmd_<name>` functions with `##` help; each calls
  `scripts/ctr.sh run <script>`. `setup` = image + hooks + build; `build` →
  `scripts/build_target.sh`; `disc` → `make_cdi.sh`; `release` sets `RELEASE=1
  EXTRA_CFLAGS=`; `soak` adds `-DDB_SOAK`; `run-hw`, `gdb-hw`, `test` (`--list`,
  `--ci` no-ROM subset). `.githooks/pre-commit` runs `./run.sh test`.
- **Decomp baked into the container image at a pinned rev, not a submodule**
  (`docker/pins.env:1-3`). Pins: `DECOMP_REV`, `KOS_REV=c22f26c…`,
  `TOOLCHAIN_PROFILE=legacy` (GCC 13.2; GCC 15 tried and abandoned), `SH4ZAM_REV`,
  `N64_REV` (gfxdis, verification only), `IMG4DC_REV` (Kazade cdi4dc),
  `IMAGE_REGISTRY=ghcr.io/beigeceleron/ssb64-dc`.
- `docker/fetch-src.sh`: shallow fetch by SHA then `rm -rf .git`. `docker/Dockerfile`
  stages: `src` (clones + applies `docker/patches/*.patch` to the decomp),
  `toolchain-build` (kos-chain, ~1 h), `arm-build` (arm-eabi for AICA), `dev` (KOS,
  gfxdis, venv numpy+pygfxd, genisoimage, `scramble`/`makeip`, cdi4dc). Env
  `SSB_DECOMP_DIR=/opt/ssb-decomp-re`, `SH4ZAM_DIR=/opt/sh4zam`; repo bind-mounted at
  `/work`. ROM never enters the image.
- `docker/patches/README.md` rules: one patch per reason; decomp N64 build stays
  byte-identical (guard on something IDO doesn't define); upstreamable only. All 6
  patches fix little-endian bitfield order or header ordering.
- Flags: `scripts/dc_env.sh:63` `-O2 -fno-PIC -fno-PIE -fomit-frame-pointer`; `:87`
  `-ffp-contract=off` (no `fmac`, floats must match N64). KOS supplies `-m4-single`
  (`src/dc/mtx.h:141`) and `-mfsrra -mfsca`. **No `-ffast-math`**
  (`scripts/test_purity.sh:17-20`: would arm FSRRA inside collision). No LTO. No
  custom linker script. Link: `kos-cc -o $(TARGET) $(OBJS) particlebanks.ld wpattrs.ld`
  (generated symbol assignments).
- Heap: 1280 KB "general heap" malloc'd at boot (`src/game/ssb64/main.c:210`); 16 KB
  KOS stack per game coroutine (`sysshim.c:236`).
- `src/game/ssb64/Makefile` (5367 lines): L142-186 OBJS; L237-247 `DECOMP_INC =
  -I../../dc/decomp -I$(SSB_DECOMP_DIR)/src -idirafter $(SSB_DECOMP_DIR)/include`,
  `DECOMP_DEFS = -D_LANGUAGE_C -DF3DEX_GBI_2 -DREGION_US -DAVOID_UB`; L275
  `DECOMP_CFLAGS` `-Wno-*` for decomp TUs only; decomp TUs compile straight from the
  image (e.g. L3599). Overlay BSS emulation L278-298 (`-fno-data-sections` +
  `objcopy --rename-section .bss=ovlN_noload`, zeroed by `src/dc/overlay.c`).
  L306-340 `MTX_BACKEND ?= SH4ZAM` (alt `SCALAR`). L319-323 `.cflags` stamp (header
  deps untracked). L197-207 `DISC=1`/`DCLOAD=1` drop `romdisk.o`. L217-226 `RELEASE=1`
  refuses `EXTRA_CFLAGS`, adds `-DSSB_RELEASE=1 -DDB_ENABLE=0 -DNDEBUG`, drops `db.o`.
- **Headers: `ultra64.h` not stubbed.** Decomp headers used as-is with selective
  shadows in `src/dc/decomp/` (see its README): `PR/ultratypes.h` (stdint),
  `PR/os.h` (~12 names; opaque `OSThread{impl,id,priority,entry,arg,stack}`,
  `OSMesgQueue`, `OSTime`; drops bcopy/bzero), `macros.h` (`#include_next` then
  `#undef __attribute__` — decomp defines it empty, which dropped `aligned(8)`;
  `mtx.h:52` static-asserts), `sys/utils.h`, etc. Struct-layout changes go in
  docker/patches, never shims.
- Debug knobs `src/dc/db.h` (417 lines, ~90 `DB_*`, all off): `DB_BOOT_SCENE`,
  `DB_IO_TRACE`, `DB_PERF`, `DB_PVR_BUDGET`, `DB_FENCE_TRACE`, `DB_HEAP_WALK`,
  `DB_STACK_GUARD`, `DB_SOAK`, `DB_GDB`, `DB_PC_ROOT`. `EXTRA_CFLAGS=-DDB_X ./run.sh disc`.

## 2. Disc pipeline (`scripts/make_cdi.sh`)
1. L106 `make DISC=1 TARGET=ssb64-disc.elf` (release: `-release` suffix).
2. L116-117 `sh-elf-objcopy -R .stack -O binary $ELF game.bin`; `scramble game.bin iso/1ST_READ.BIN`.
3. L120-140 copy `romdisk/` + `disc/` into `iso/`; drop loose files bundled in `models.bnd`.
4. L145-150 `disc.id` build stamp (runtime probes it to pick `/cd`).
5. L154-155 empty `pad.bin`. 6. L161 `disc_layout.py iso -o sort.txt`.
7. L171-172 `makeip -f $(ip_check.py --makeip-args) IP.BIN` (`-a JUE -p 0799810 -n T-00000 -e V1.000 -b 1ST_READ.BIN -c SSB64-DC -g SSB64-DC`).
8. L174 `mkisofs -C 0,11702 -V SSB64DC -G IP.BIN -l -sort sort.txt`.
9. L180-183 pad so the image ends 20 s before 80-min lead-out (`disc_layout.py:155-176`; CAV → outer edge ~2x faster). `SSB_DISC_PAD=0` skips.
10. L185-192 `mkisofs -o data.iso`; `disc_check.py`; `ip_check.py --quiet`; `cdi4dc data.iso out.cdi`.
- `disc_layout.py` (868 lines) orders by `disc_order.txt` (measured read order) then
  manifest groups. `prefetch_lists.py` turns a `-DDB_IO_TRACE` serial log into
  per-scene read-ahead lists. `run_hw.sh`: `DCLOAD=1` build, `dc-tool-ser`/`-ip -x
  $ELF -c romdisk`. GDB via `gdb_hw.sh` on localhost:2159.

## 3. Rendering
- **No runtime display-list interpreter** (`src/dc/scvsresults.c:1017`). Build-time
  `tools/lib/ssb_assets.py` `MeshBaker` (L2097) replays F3DEX2 DLs with pygfxd
  (32-slot vertex buffer, joint matrices, materials, `rendermode_list` L1870,
  `bake_tile` L1968). Output: per-joint vertex runs + material batches of indexed
  tris. Verified by an independent second interpreter (`model_pack_check.py`) and gfxdis.
- Pack format `src/dc/fighter.h`: `FPackHeader` L48-82 ("SSBPACKA"); `FPackJoint`
  L646; `FPackVtx {float x,y,z,nx,ny,nz,u,v; u8 alpha,joint; u16 pad}` 36 B L652-660;
  `FPackBatch` L779-789 (`bucket` bits OP/PT/TR, NOZ, FILT_POINT, ENVLERP, TEXLERP,
  CULL_BACK/FRONT, TEXGEN, ZALWAYS; `alpha_src` bits); `FPackTex` L813-831 (PAL4 /
  ARGB1555 / ARGB4444 / EXTERN); `FPackAnim` L849-864 keeps native words — the
  decomp's own animation interpreters run unmodified (bit-exact check).
- List mapping (`fighter.h:661-668`): `FORCE_BL` → TR; `CVG_X_ALPHA` w/o FORCE_BL →
  PT; else OP.
- `src/dc/dcpvr.h`: `dc_pvr_init()` L49-64 `pvr_init` OPB `{16,0,16,0,16}`,
  `vertex_buf_size=512K`, `opb_overflow_count=3`, `PVR_SET(PVR_PT_ALPHA_REF,0x80)`.
  `dc_pvr_vram_fence()` L117-124 before every VRAM upload/free (enforced by
  `vramfence_check.py`).
- Poly headers `fighter.c:248-370` `compile_batch_txr`: `pvr_poly_cxt_txr`; PAL4 via
  `PVR_TXRFMT_PAL4BPP|PVR_TXRFMT_4BPP_PAL(...)`; bilinear unless FILT_POINT;
  `txr.env = ALPHA_TEX ? MODULATEALPHA : MODULATE`; CULL_BACK → `PVR_CULLING_CW`;
  `gen.specular = PVR_SPECULAR_ENABLE` on every batch (offset color carries "+ENV"
  and fog); ZALWAYS → `DEPTHCMP_ALWAYS` + no write. Headers compiled once at load
  into `memalign(32)` (L381; movca.l hazard); two per batch (own list + TR copy for
  fades). Palettes: `pvr_set_pal_format(ARGB1555)`, 64 banks of 16 (L1320).
- **CPU transform with sh4zam** (`src/dc/mtx.h`): `mtx_mul` →
  `shz_xmtrx_load_apply_store_unaligned_4x4(out, b, a)` (L63-66); `mtx_load3(a,b,c)`
  → `shz_xmtrx_load_transpose_unaligned_4x4(a); shz_xmtrx_apply_transpose_unaligned_4x4(b); …(c)`
  (L161-165); `mtx_xform` → `shz_xmtrx_transform_vec4` (L175-179); `mtx_sincos` →
  `shz_sincosf` when |a|<1024 (L199-213); `mtx_dot3` → `shz_vec3_dot`; `mtx_rsqrt`
  → `shz_inv_sqrtf`. Joint walk `fighter.c:1881-1895`; vertex loop L1955-1996
  (`mtx_load3(proj, mv, f->mtx[cur])` per joint change; `mtx_xform` per vertex);
  near-plane + clip-plane clipping in clip space (`src/dc/clip.c`); per-batch vertex
  cache L2578; `emit_vtx` L2415-2456 (one 1/w, viewport map, `z = zbase +
  gZBias*gZScale*invw`). Tris submitted one at a time with `pvr_prim` (L2461-2467) —
  no `pvr_dr`, no DMA, no strips. Textures via `pvr_txr_load`.
- Frame loop `src/dc/taskman.c`: `syTaskmanRunFrame` L1207 single-threaded;
  draw L819-850 `pvr_wait_ready(); pvr_scene_begin();` then `syTaskmanDrawPasses`
  L761-816 runs the scene draw three times (OP, PT, TR) with
  `pvr_list_begin/finish`; `pvr_scene_finish()`. `src/dc/objdisplay.c` mirrors the
  decomp's display walk with a 10-deep matrix stack.
- Resolution: `main.c:169` `vid_set_mode(DM_640x480, PM_RGB565)` (VGA 480p on VGA
  cable, else NTSC 480i, never 50 Hz). Game coords 320x240 with `gDCScreenScale=2`
  (`sysshim.c:37-45`). `pvr_set_zclip` at main.c:179.

## 4. Memory & assets
- `assetroot.c` (3034 lines): probes `/pc` (dcload) → `/cd/disc.id` → `/rd` → host
  `romdisk`. Raw `fs_read` into `memalign(32)` buffers so `iso_read` takes GD-ROM
  DMA (L48-63). One I/O lock (`io_check.py`). Per-scene `io:` counters;
  `asset_io_expect_none` flags reads during a match.
- `models.bnd` (`ssb_bundle.py:12-25`): `"BND2"`, count, `index_off=16`, `data_off`,
  `{char name[32]; u32 off,size}`, 32-byte aligned, measured read order.
- Hold list `asset_hold_set`; read-ahead `asset_prefetch_*` loader thread (64 KB per
  lock turn, keeps `ASSET_PREFETCH_RESERVE=1.5MB` free, L212-267).
- Cart-DMA shim `src/dc/dma.c`: `syDmaReadRom(rom_addr,dst,size)` maps 18 ROM ranges
  (L36-55) to `<file,offset>`, 32 KB bounce buffer. All other ROM access replaced by
  per-scene packs/banks.
- Budgets: 1280 KB general heap; 512 KB TA vertex buffer (= 1 MB VRAM, KOS keeps
  two); 2 MB sound RAM per scene (`sndres.c`); romdisk ELF ~14 MB → disc build exists.

## 5. Audio
- Custom ARM7 firmware `src/dc/aica/main.c` replaces KOS `stream.drv`, serves the same
  `aica_cmd_iface` queue, plus the port's own integer sequencer (`src/dc/seq/seqcore.c`,
  same source on ARM/SH-4/host) on channels 0-23; SH-4 side `bgmarm.c` sends
  `DC_AICA_CMD_SEQ`. Build (Makefile:3511-3558): `arm-eabi -mcpu=arm7di
  -fno-schedule-insns -marm -ffreestanding`, `-Ttext=0`, size guard `_end < 0xA000`,
  embedded via `fwblob.S`; loaded by `dcaica.c:19-35` (`spu_disable`, `spu_memset_sq`,
  `spu_memload`, `spu_enable`, `snd_mem_init(DC_AICA_RAM_START)`).
- Samples: VADPCM decoded (`tools/lib/ssb_vadpcm.py`) → Yamaha 4-bit ADPCM
  (`tools/lib/ssb_adpcm.py`, `AICA_SM_ADPCM`); music bank 4 MB PCM16 → 1 MB.
- Reverb: `ssb_reverbexport.py` turns `AL_FX_CUSTOM` params into an AICA DSP program
  (taps rescaled 32006 → 44100 Hz). SFX: `fgm.c` ports `n_env.c` voice/note
  interpreters (AICA channels 32-55).

## 6. libultra/OS (`src/dc/sysshim.c`)
Single-threaded frame loop. Mesg queues = ring buffers; blocking on empty/full
`abort()`s with a message (L170-217). GObj threads → coroutines on KOS threads
(16 KB stack, run/yield semaphores, L219-457); host uses `ucontext`. `osGetTime` =
`timer_us_gettime64`. Pacing from `pvr_wait_ready`/vblank; tic waits `vid_waitvbl`
(taskman.c:296-339). No `osSetTimer`/`osViSwapBuffer`. Input `src/dc/input.c`:
`maple_enum_dev(port,0)`; table L239-247; triggers ≥128 → Z/R; stick `joyx*80/128`,
Y inverted (L277-289). Save: SRAM → `vmusave.c` (4 KB array, dirty flag, writer
thread, "SAVING…" notice); `vmucard.c`, `dcmemcard.c`.

## 7. Testing
- Host cross-tests (`scripts/test_host.sh`): port C + unmodified decomp C compiled
  on the host with `-DFT_HOSTTEST` and `hoststubs/` (fake `kos.h`, `dc/pvr.h`);
  physics fuzzed against verbatim decomp copies; scenarios on real ROM attributes.
- Oracles (`scripts/test_oracle.sh`): each exporter checked against an independent
  reader (gfxdis, second F3DEX2 interpreter, decomp C compiled as `*_oracle.c`,
  VADPCM/MIDI decoders, bit-exact figatree).
- Purity (`scripts/test_purity.sh`, `fpu_check.py`): forbids FSCA/FSRRA in objects
  whose floats are pinned to the N64 (L41-80), forbids `fmac` everywhere.
- Tree rules `tools/check/` (69 files): `vramfence`, `polyhdr`, `standin`,
  `overlay`, `io`, `rom_leak_check` (no ROM bytes in repo).
- DIVERGES discipline (CONTRIBUTING.md): port function-by-function with decomp
  names; every deliberate difference marked `DIVERGES` with reason; new knobs in
  `db.h`, off by default; show fixes on hardware. `PROVENANCE.tsv` tags every file
  ORIGINAL/DERIVED/MIXED/SGI.

## 8. sh4zam
All in `src/dc/mtx.h`: `shz_xmtrx_transform_vec4`,
`shz_xmtrx_load_transpose_unaligned_4x4`, `shz_xmtrx_load_apply_store_unaligned_4x4`,
`shz_xmtrx_apply_transpose_unaligned_4x4`, `shz_vec3_dot`, `shz_sincosf`,
`shz_inv_sqrtf`, types. Header-only on target. Host SW fallback:
`src/dc/test/Makefile:30-40` links `$(SH4ZAM_DIR)/source/sw/shz_xmtrx_sw.c`
(`SHZ_BACKEND=SHZ_SW` uses exact libm → host checks algebra only).

## Copy verbatim vs adapt (for wings64-dc)
**Copy:** `run.sh`/`ctr.sh`/`dc_env.sh`/`build_target.sh`/`target.sh`; `docker/`
structure (pins.env, fetch-src.sh, apply-patches.sh, multi-stage Dockerfile);
`make_cdi.sh` + `ip_check.py` (retitle); `disc_layout.py` pad logic; `ssb_bundle.py`
(BND2); `.cflags` stamp; RELEASE/DISC/DCLOAD switches; `src/dc/mtx.h`, `dcpvr.h`,
`clip.c`, `stackguard.*`; `assetroot.*`; `sysshim.c` shim + `PR/os.h`/`ultratypes.h`/
`macros.h` shadow pattern; `input.c` skeleton; `vmusave.c`; `db.h` convention;
`src/dc/test` host test + sh4zam SW fallback; `test_purity.sh`/`fpu_check.py`;
`rom_leak_check.py`; `PROVENANCE.tsv`; pre-commit hook; AICA firmware scaffolding
(`aica/main.c`, `dcaica.c`, `fwblob.S`) + ADPCM encoder.
**Adapt:** OBJS lists; which decomp TUs compile unmodified; `src/dc/decomp/` shadows;
docker patches for PW64 bitfield structs; `DECOMP_DEFS` (Fast3D, not F3DEX2);
`MeshBaker` replaced by load-time native bake using PW64's own parsers; animation
(UVAN) kept native + decomp interpreter; `dma.c` → `_uvMediaCopy` choke point;
terrain/LOD; audio sequencer (check PW64 seq format before reusing `seqcore`);
`disc_order.txt`/prefetch re-measured; three-pass OP/PT/TR draw + `emit_vtx` z
mapping fit as-is, re-tune `gZBias`/`zclip` for large outdoor scenes.
