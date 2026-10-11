# sf64-dc, mk64-dc (jnmartin84) and DCA3 (skmp): lessons

## A) sf64-dc (CC0) — most relevant: open sky, large terrain, 60 fps target
- Toolchain: KOS **v2.2.1**, GCC **14.3.0** (`README.md`; `docker/Dockerfile` clones
  KOS `-b v2.2.1`, dc-chain `toolchain_profile=14.3.0`, builds mkdcdisc with meson).
  `LIBS := -lc -lm -lkallisti` (Makefile:547). Flow: `make init` → decompress
  (comptool.py + mio0) → extract (splat) → assets (Torch → C; `strip_dc_syncs.py`)
  → objects.
- **Rendering: runtime Fast3D-family interpreter, raw PVR, no GLdc.**
  `src/gfx/gfx_retro_dc.c` (4156 lines): `gfx_run_dl` :3695 (opcode `w0>>24` :3703),
  `gfx_sp_vertex` :1482, `gfx_sp_tri1` :2562, lighting, texgen, software
  clip/cull/scissor, CI4 palette cache. Hot path in section `.text.hot.gfx` kept
  under 8 KB for the direct-mapped I-cache (:36-45). Textures decoded at runtime into
  a 1024-bucket cache keyed by address (:172, :663-720) as ARGB4444/ARGB1555
  (RGBA5551 rotated `((c&1)<<15)|(c>>1)` :873, :910); static textures twiddled,
  dynamic non-twiddled (:850-852). No VQ, no mipmaps.
  `src/gfx/gfx_pvr.c` (960 lines): store-queue direct render (`pvr_dr_state_t`);
  depth/fog/blend from othermode; `pvr_init` bins 32/0/32/0/32, 768 KB vertex buffer,
  DMA on, autosort on (:379-387); 640x480 (or 320x240 `LOWRES`) RGB565 (:790-792).
- sh4zam vendored as `include/sh4zam.h` + `src/shz_mem.s`; top calls
  `shz_sqrtf_fsrra`, `shz_fast_invf` (old name), `shz_xmtrx_*`. KOS `mat_*` only in comments.
- `-DGBI_FLOATS` (Makefile:97) makes `Mtx` a `float m[4][4]` (`include/PR/gbi.h:1116-1139`)
  → no s15.16 conversion. `strip_dc_syncs.py` removes ~5k Pipe/Tile/LoadSync
  commands from generated DLs.
- libultra shim `src/ultra_reimpl.c`: non-blocking ring-buffer mesg queues
  (:86-123); `osSetTimer`, `osGetTime`, no-op cache calls; EEPROM → VMU `sf64.rec`
  (:219-395). Only `src/libultra/gu/*` kept. Threads flattened into one main loop
  (`sys_main.c:~315-345`); audio on a KOS thread prio 9 via `thd_create_ex`
  (:282-295), woken by vblank, catch-up ≤3 ticks (:455-505).
- **Segment trick:** `SEGMENTED_TO_VIRTUAL` identity (`include/macros.h:26`);
  `segment_init()` (`sys_main.c:80-95`) uses the SH4 MMU to map 64 KB pages from
  `0x0N000000` to RAM buffers `SEG_BUF[]` (`src/dmatable.c:94-105`).
- ROM → disc: Torch asset C compiled with `-fno-common -fno-toplevel-reorder
  -fno-zero-initialized-in-bss` (Makefile:360-373); each group linked at its N64
  segment base (`sh-elf-ld -EL -Ttext=0N000000`) and objcopied to `sf_data/<name>.bin`
  (:656+); main ELF uses `-Wl,--just-symbols=` (:544). Runtime `Load_RomFile`
  (`src/engine/fox_load.c:30-52`) `fs_read`s `/cd/sf_data/X.bin` into `SEG_BUF`.
  Data root probed `/pc`, `/cd`, `/sd/sf64-dc`, `/ide/sf64-dc` (`sys_main.c:386-428`).
- Audio: AICA hardware mixing; libultra sequencer on SH4; `src/audio/aica_synth.c`
  (adapted from an OoT driver) 64 channels with ARAM cache, `snd_sh4_to_aica`; long
  samples streamed. Build (Makefile:622-641): `tools/aica/sf64_emit.py` VADPCM →
  Yamaha ADPCM pool `sf_data/adpcm_pool.bin` + `aica_sample_table.c`. Big-endian bank
  data byte-swapped on load (`audio_load.c:648-680`, `:1414-1429`; `audio_seqplayer.c:588`).
- Flags: environ.sh `-O3 -flto=auto -ffat-lto-objects -fomit-frame-pointer -fbuiltin
  -ffast-math -ffp-contract=fast -mfsrra -mfsca`, `KOS_SH4_PRECISION=-m4-single-only`.
  Makefile default `-Os` (:142); hot `-O3`/`-O2` (`gfx_retro_dc` -O3, `gfx_pvr` -O2,
  mixer -O3, `sys_math` -O3, gu -O3; :346-357). CFLAGS (:97) `-DAVOID_UB
  -DNON_MATCHING -fno-toplevel-reorder -falign-functions=32 -fno-data-sections
  -DGBI_FLOATS`. No `-fno-strict-aliasing` (mk64 has it).
- CDI (Makefile:605-611): `mkdcdisc -a jnmartin84 -n "Star Fox 64" -r 20251205 -i
  mrlogo.mr -f sf64.ico -d sf_data -e sf64.elf -o X.cdi`; `-N` for ODE, padding on for
  CD-R. DreamShell ISO: `mkisofs -G ip.bin ... 1ST_READ.BIN=sf64.bin` (:615).
- README: real hardware only; emulators unsupported; SD gives stuttering audio
  (mixer needs real-time); VMU save 3 blocks; no fps figure.

## B) mk64-dc — differences
- KOS master `680d1862`, GCC 13/14/15. Segments via software table
  (`src/racing/memory.c:118`), common data fread into `COMMON_BUF` (`src/main.c:1283-1310`).
- sh4zam linked as library (`-lsh4zam`, Makefile:817).
- Assets stay compressed (MIO0 geography, TKMK00 textures) decompressed at runtime.
- `generate_dc_data.sh` objcopies per-course bins into `dc_data/`; injects
  `SegmentRomStart/End` via `objcopy --add-symbol`.
- `loader.c`: 1ST_READ with progress bar + VMU animation; reads ~9 MB `mk64.bin` into
  `progbuf`, calls its own `arch_exec_` copy ("latest KOS version has broken
  arch_exec()" :265-268, :341). CDI: `mkdcdisc -f ghost.ico -f kart.ico -f mk64.bin
  -d dc_data -e loader.elf -n "Mario Kart 64" -v 3 -N` (Makefile:852); alt
  `mkisofs -C 0,11702` + `cdi4dc` (:839-845).
- `memcpy32.S` = Falco Girgis 32-byte-aligned memcpy from KOS `koslib/memcpy.s`.
- `safe_gcc.mk` lists are vestigial (decomp inheritance). Real per-file: `-O3` on
  gfx_retro_dc, math_util, collision (:439-442); `-fno-lto` on course data (:443-451);
  global `-Os -fno-strict-aliasing -fwrapv -DGBI_FLOATS -DAVOID_UB` (:344).
- README: "30 fps 4 player with music"; real hardware only; ghost save 67 VMU blocks.

## C) dca3-game (GTA III, re3)
- Only `dreamcast/Makefile` targets DC. Own KOS fork `vendor/dca3-kos`.
  `rwdc.cpp:21-24` static_assert for a `PVR_TXRFMT_STRIDE` bug in some KOS versions.
- Flags (Makefile:102-123): default `-Os` (RAM), `-O3` with `WITH_32MB`; explicit
  `OBJS_O3` list (:86-96); `Cam`/`Camera` built `-fno-fast-math` (:98-100, :153);
  `-flto -fipa-pta -fno-strict-aliasing -fwrapv -mfsrra -mfsca`; `--gc-sections`.
- Disc (:168-236): scramble → `1ST_READ.BIN`, `makeip ip.txt IP.BIN`, `mkisofs -C
  0,11702 -G IP.BIN` → `cdi4dc`; or `mkdcdisc -e dca3.elf -d repack-data/gta3/ -N -n
  DCA3`; `FOR_DISC=1` drops `-N`.
- Asset repack (:282-495): game's own loader built for host with `vendor/koshle`
  (high-level KOS PVR emulation, `-DDC_TEXCONV -DDC_SIM`) writes DC-native files;
  textures downsampled (`-d HALF|QUARTER`) and VQ-encoded via `dreamcast/pvrtex`
  (`-c small -d`) or pvrtool (`-VQ -VQDITHER`) (`rwdc.cpp:4070-4100`), loaded
  `PVR_TXRFMT_TWIDDLED|VQ_ENABLE`. Mipmaps TODO (:3856). Geometry pre-stripped
  (TriStripper); `gen-mesh-variants.py`.
- Renderer `vendor/librw/src/dc/rwdc.cpp` (5686 lines): raw PVR DR, KOS `mat_*`
  (no sh4zam), bins 16/0/8/0/8, 2 MB vertex buffer, autosort off (:4329-4336).
  `alloc.cpp` VRAM allocator on 2 KB boundaries (VQ codebooks don't cross pages) with
  defrag. `sim.mk` PC simulator build.
- Streaming `src/core/CdStreamDC.cpp` pthreads + `O_DIRECT` read thread; models
  evicted under pressure (`Streaming.cpp:1163`).
- AICA `src/audio/sampman_dc.cpp`: `aica_play_chn/stop/volpan` via `snd_sh4_to_aica`
  (:40-110); streams 2 channels each, double-buffered ADPCM in ARAM, refilled by
  polling channel `pos` via `g2_read_32` (:315-385). `aud2adpcm` for SFX (`-raw`),
  streams `.APM`.

## Lessons for wings64-dc
1. Runtime Fast3D interpreter on raw PVR (`gfx_retro_dc.c` + `gfx_pvr.c` lineage) is
   proven for N64 decomps at 30-60 fps. Start there; bake native later only where
   profiling says so.
2. Keep segmented pointers unchanged where possible (sf64's MMU trick) — but PW64's
   K0 addressing maps onto SH-4 P1 with a single OR (see Birdman64 brief), which is
   simpler. Rule in this port: MMU stays off (store-queue/QACR cost).
3. `-DGBI_FLOATS` style float `Mtx` eliminates s15.16 conversion. PW64 bulk-copies
   `Mtx4F` from ROM; convert at load.
4. `-Os` globally, `-O3` on gfx/math/mixer; `-fno-strict-aliasing -fwrapv
   -funsigned-char -ffp-contract=off` as the IDO-era safety net; consider
   `-fno-fast-math` on camera/physics TUs (dca3) and measure.
5. Pin KOS + GCC and ship own `environ.sh` with `-m4-single-only -mfsrra -mfsca`.
6. AICA hardware voices (`aica_synth.c` lineage) + offline VADPCM → Yamaha ADPCM pool;
   sequencer on SH4 in a vblank-woken thread with bounded catch-up.
7. Runtime texture decode → hash cache (ARGB1555/4444, twiddled) is fine for PW64's
   ~400 small textures (`size ≤ 0x1000`); VQ only if VRAM gets tight.
8. 2 KB-aligned VRAM allocator idea (dca3) if texture churn grows.
9. `mkdcdisc -d data -e elf` (+`-N` for ODE/emulator; padding on for CD-R). Probe
   `/pc`, `/cd`, `/sd`, `/ide` for the data root.
10. Real hardware is truth. Keep I-cache hot section (<8 KB) for the gfx core.
