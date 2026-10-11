# Birdman64 and sm64-dc: platform-layer reference

## A) Birdman64 (NoahVl, MIT) — the closest existing platform layer for THIS game
Compiles the PW64 decomp's C with clang/Zig against a Rust libultra replacement.
Proves the decomp compiles on a modern compiler with a known patch set. Paths
relative to the Birdman64 repo.

**Docs = the spec.** `docs/notes/native-build.md`: §1 complete libultra
undefined-symbol table, §2 pointer hazards, §3 endianness hazards, §4 threading,
§5 compiler/UB hazards, §7 patch mechanism, §10 SysV bitfield layout audit (the
right reference for sh-elf-gcc). Also `formats.md`, `gfx.md`, `audio.md`,
`renderer.md:110-170` (perf numbers). `ROADMAP.md`: remaining renderer gaps
(dither, coverage AA, detail textures, canopy fringes, lighting).

**C build** (`crates/pw64-cbuild/src/lib.rs:81-222`):
- Compiled: `src/kernel`, `src/app`, `src/libultra/audio`, `src/libultra/sp` (197 files).
- Excluded: `src/libultra/{os,io,libc,debug,vimodes}`, `src/rsp`, all asm.
- Defines: `_LANGUAGE_C VERSION_US BUILD_VERSION=VERSION_D _FINALROM NDEBUG TARGET_N64
  NON_MATCHING AVOID_UB RECOMP_BUILD`. NOT `__sgi`, NOT `F3DEX_GBI` (game is Fast3D).
- Codegen: `-nostdinc -std=gnu11 -funsigned-char -fwrapv -fno-strict-aliasing
  -ffp-contract=off -ftrivial-auto-var-init=zero -fgnuc-version=4.2.1 -O3`.
  `-funsigned-char` (IDO chars unsigned); `-ffp-contract=off` (VR4300 has no FMA);
  zero-init because clang turned `hangGliderMovementFrame`'s uninitialised `sp5B`
  into `int3`.
- `-Werror` on pointer-width warnings; implicit-int / implicit-function-declaration /
  return-type downgraded to warnings.
- Headers: decomp headers used + patches + shadows in `crates/pw64-game/native/include/`:
  `segment_symbols.h` (ROM offsets as constants; app overlay copy becomes a no-op),
  `libc/stdint.h`, `libc/stddef.h`, `stdlib.h`, `math.h`, `stdio.h`; force-included
  `native/pw64_native.h` (`PW64_U32`/`PW64_PTR`, `PW64_SWAP(p,n,T,"layout")`,
  `PW64_LONG32`). Patcher: strict no-fuzz unified diff (`lib.rs:261`).

**The 80 patches** (`crates/pw64-game/patches/`) by cause:
- Endianness (NEEDED on SH-4): raw ROM structs swapped after `_uvMediaCopy`.
  `texture.c` 20 `PW64_SWAP` (Vtx, Mtx u16 halves, Mtx4F, ParsedUVEN, UVTX Gfx words),
  `task.c` 23, `spath.c` 12, `level.c` 7, plus `anim.c`, `user_paths.c`, `demo*.c`,
  `text_data.c`, `memory.c`, `audio_seq.c`. `code_8170.c` env tint loops rewrite
  texels as u16 in place; `code_A8C30.c` byte table packed inside s32 array;
  `font.c` Bitmap loader. Audio bank/seq via field-wise deserialisers
  (`native/src/pw64_audio_load.c`, replacing in-place `alBnkfNew`).
- Bitfield order (NEEDED): IDO packs MSB-first, GCC LSB-first.
  `include/kernel/uv_filesystem.h.patch` reverses bitfields; `snap.c` converts the
  EEPROM photo record field by field; `gbi.h.patch` packs cmd/pad into one word, the
  `setimg.cmd` read (`texture.c:807`) becomes `w0>>24`.
- 64-bit pointers (~35 patches, mostly NOT needed on 32-bit SH-4): `_uvMemAlloc`
  returns s32; bnkf/load/reverb/cseq offsets; `string.c`; `debug.c`.
- Real decomp UB/bugs (KEEP ALL): `audio_manager.c` block-scope `customFxParams`
  used after lifetime → static; `test_summary.c` `sp5C[3]` overrun → `[3+2]`;
  `system.c` `gSiContStatus` → `[4]`, `app_entrypoint` prototype; `demo.c` RHDR
  stride 24 not 20; `synthesizer.c` ALParam pool stride must fit largest param
  struct (re-check `sizeof` on SH-4).
- Cooperative-scheduler yields (`title_screen.c`, `audio_seq.c` `uvaSeqStop` spin,
  `snap.c`): needed if threads don't preempt at the right priority.
- NOT needed on DC: ~20 frame-rate patches (`pw64_rate.h`, birdman*/gyrocopter), ~6
  widescreen/HUD/OLED patches (`hud.c`, `chan.c`, `code_7150.c`, `code_D2B10.c`, `font.c`).

**DC-specific consequences (§2):**
- 39 `0x80xxxxxx` literals: audio heap 0x80000400 (`audio_manager.c:40`),
  framebuffers 0x800DA800/0x80100000, z-buffer 0x803DA800; sites `memory.c:41-76`,
  `graphics.c:105-107`, `sched.c:219`, `system.c:223-227`. On DC, P1 0x80000000 is
  boot ROM and the MMU cannot remap P1 → patch to an arena.
- Works for free: DC RAM 0x8C000000 has bit 31 set, so the "bit31 clear = ROM offset"
  test (`memory.c:146,195`, `filesystem.c:89`) still works. `K0_TO_PHYS` (mask 29
  bits) and `OS_PHYSICAL_TO_K0` (OR 0x80000000) map onto SH-4 P1/physical exactly,
  so Gfx w1 addresses translate with a single OR.

**libultra surface the game actually needs** (`crates/pw64-platform/src`):
- Threads: `osCreateThread/StartThread/StopThread/SetThreadPri/GetThreadPri/GetThreadId/YieldThread`.
- Messages: `osCreateMesgQueue/SendMesg/RecvMesg/JamMesg/SetEventMesg` (`os/mesg.rs:74,129`).
- Timers: `osGetCount/GetTime/SetTimer/StopTimer`.
- Init/cache: `osInitialize`, `osSetIntMask`, `osInval/WritebackDCache(All)`,
  `osInvalICache`, `osVirtualToPhysical` (`os/misc.rs:24-49`).
- PI: `osCreatePiManager`, `osPiStartDma` (`pi.rs:249`: synchronous memcpy from ROM,
  then post the `OSIoMesg`), `osPiReadIo/RawReadIo`, `mio0_decompress` (`pi.rs:312`).
- VI: `osCreateViManager/ViSetMode/ViSetEvent/ViBlack/ViSwapBuffer` (`os/vi.rs:302`,
  latches at next retrace), `osViGetCurrent/NextFramebuffer`, `osViSetSpecialFeatures`.
- SP: `osSpTaskLoad/StartGo/Yield/Yielded` (`headless.rs:200`; gfx task deferred, run
  at pseudo-priority 100).
- AI: `osAiSetFrequency/SetNextBuffer/GetLength` (`ai.rs`).
- SI/EEPROM: `osContInit/StartReadData/GetReadData` (`headless.rs:381`);
  `osEepromProbe/LongRead/LongWrite` (`:512`; 0x800-byte raw image).
- Data symbols: `osTvType`, `osMemSize = 0x800000`, `osViModeTable`, ucode symbols
  (`stubs.rs:25-54`). `gu*` NOT needed (only `sqrtf`).
- Thread model: single host thread, coroutines, strict libultra priority rules,
  switches only on block/send/start/setpri; kernel thread pri 0 parked; interrupts
  (VI 60 Hz, SP/DP done) delivered at OS-call checkpoints. Table in native-build.md §4.

**Formats** (`crates/pw64-formats/src`; layout tables in module docs `uvmd.rs:7-23`,
`uvtx.rs:7-15`, `uvct.rs:8-15`, `uvtr.rs:9-14`, `uven.rs:9-13`, `uvbt.rs:7-16`; also
`uvan`, `uvlv`, `uvtp`, `uvft`). ALL of UVMD/UVTX/UVCT/UVTR/UVLV/UVEN/UVTP/UVFT/UVBT/UVAN
fully parsed; UVSX in `pw64-audio-data`. Filesystem: IFF FORMs, 1272 files, UVRM
index at ROM 0x0DE720; `GZIP` block = (inner tag, size, MIO0 stream)
(`pw64-rom/src/fs.rs:86`, `mio0.rs:6`, 70 lines). Engine MIO0-decompresses every
GZIP block (UVTX/UVMD/UVCT COMM, UVFT IMAG) at level load. Audio raw VADPCM;
resampler LUT at ROM 0x51C30.
- UVTX: images already in TMEM order (odd rows 32-bit-swapped), loaded with
  `LoadBlock dxt=0` → deswizzle needed (`tmem.rs`). Formats: RGBA16 249, I4 96, IA8 56,
  I8 20, IA4 20, IA16 9; UVBT adds RGBA32. **No CI/TLUT anywhere.** Usually 6 mip
  levels; non-power-of-two heights common (e.g. 32x47); `size` ≤ 0x1000.

**Gfx** (`docs/notes/gfx.md`, `crates/pw64-gfx/src/interp.rs`):
- Plain Fast3D, 16-vertex cache, `G_TRI1` only (no TRI2), 32 distinct opcodes:
  RSP `VTX MTX POPMTX DL ENDDL MOVEMEM(VIEWPORT) MOVEWORD(SEGMENT, CLIP, FOG, NUMLIGHT,
  LIGHTCOL) TEXTURE SET/CLEARGEOMETRYMODE`; RDP `SETTILE SETTILESIZE SETTIMG LOADBLOCK
  SETCOMBINE SETPRIM/FOG/FILL/BLENDCOLOR SETSCISSOR SETCIMG SETZIMG SETOTHERMODE_H/L
  syncs TEXRECT+RDPHALF FILLRECT`. Never: `LOADTLUT LOADTILE TEXRECTFLIP CULLDL SETPRIMDEPTH`.
- Render modes: always 2-cycle, `G_RM_FOG_SHADE_A` or `PASS` in cycle 1. 13 combiners;
  top one `0x26a004_1f1093ff` = (T1−T0)·LOD_FRAC+T0 then ·SHADE (~65% of flight draws).
  No LOD/detail/sharpen modes enabled → treat TEXEL1 as TEXEL0.
- Matrix stack 11 levels. Fog fm = 128000/(max−min).
- Tricks: z-buffer cleared with FILLRECT where cimg==zimg; shadow volumes draw with
  cimg==zimg; `uvCopyFrameBuf` (photo album) and snow do CPU framebuffer writes.
- Flight load per frame: 2346–3767 tris, 8–13k commands, 63–312 draws, ~177–224
  texture loads (`renderer.md:118,164`).

**Audio** (`docs/notes/audio.md`): classic libaudio alSyn, 32 PVoices, 256 updates,
`AL_FX_CUSTOM` 8-section reverb, 22050 Hz. One `ALCSPlayer` (16 ch) music + one
`ALSndPlayer` (16 sounds) SFX. Every wave VADPCM at 22050 Hz, loops count −1. Music
57 waves; SFX (UVSX) 107. 31 compact sequences, division 768. Frame 368 samples with
only 100 samples (~4.5 ms) AI lead (caused Birdman's dropouts). Acmd HLE
`crates/pw64-audio/src/abi.rs` (611 lines). Three lowpass reverb sections are
silent on real hardware too.

## B) sm64-dc (jnmartin84, branch ootpvr; no top-level license — gfx core is Emill/MaikelChan BSD `src/pc/gfx/LICENSE.txt`)
- Makefile forces `GRUCODE := f3dex2e` (`Makefile:54`); F3D `#else` branches present
  (e.g. `G_TRI1` `/10` decode `gfx_retro_dc.c:2412-2420`) but unexercised.
- sh4zam in `gfx_retro_dc.c` (NOTE: uses OLD sh4zam names — `shz_xmtrx_trans_vec4`,
  `shz_matrix_4x4_t`, `shz_xmtrx_load_4x4_apply_store`, `shz_fast_invf` — these do
  NOT exist in current sh4zam; translate per `docs/sh4zam-mandate.md`).
- Data: 32-byte `LoadedVertex` (:71-90); 32+4 vertex slots; `clip_rej` outcodes
  (:96, :785-804). Matrices: fixed→float then load-apply-store (:712-771). XMTRX
  loaded once per `G_VTX` (:815), transform per vertex (:839, :936). Per-vertex fog
  coefficient (:262-269, :925).
- Clipping: software Sutherland–Hodgman near plane only (z ≥ −w), 1–2 tris
  (:1163-1217); x/y overhang left to PVR (guard band); `pvr_set_zclip(0)`
  (`gfx_pvr.c:750`); scissor no-op (`gfx_pvr.c:562`).
- Tris: each `G_TRI1` an independent 3-vertex strip (EOL on 3rd vertex,
  `gfx_pvr.c:250,277,618`). No strip joining.
- Screen: x/y = viewport map of clip/w (:234-249, :1534-1536); z = 1/w,
  `DEPTHCMP_GEQUAL`; decals ×1.003 (:223, :1546); z-off pinned 0.00001; ortho
  overlays incrementing `screen_2d_z` (:1490-1497).
- Combiner evaluated per vertex into argb + oargb, offset color always on
  (:1005-1161); `derive_pvr_texenv`; single texture (TEXEL1 = TEXEL0).
- Lists: `FORCE_BL` → TR; cutouts (CVG_X_ALPHA or texel alpha + alpha compare) → PT;
  else OP (:1435-1460). Blend factors from N64 blender (:276-289).
- Poly headers (`gfx_pvr.c`): six base headers {col,tex}×{OP,PT,TR} compiled once
  (:104-111, :173-191); per draw bit-patch texture/depth/fog/blend (:196-233);
  re-emit only when dirty. OP streams via DR store queues all frame; PT and TR
  buffered (8192 verts / 512 batches each, :126-149) and flushed at frame end (:772-790).
- PVR init: bins 32/0/32/0/32, 512 KB vertex buffer, DMA on, autosort on (:340-348);
  PT alpha ref via 0xA05F811C (:703); dither off; RGB888.
- Resolution 640×480 default (2× N64), `LOWRES=1` 320×240 (:324-331, :679-694).
- Texture cache: 1024-bucket hash by RAM address, 1024 pool entries (:113-117,
  :425-473); wiped only via `nuke_everything` (`gfx_pvr.c:519`) → PW64 reuses its
  level heap, so call on level load. Conversion (:479-689): RGBA16/IA4 → ARGB1555;
  RGBA32/IA8/IA16/I4/I8 → ARGB4444; linear row reads (PW64 needs TMEM deswizzle
  first). Upload padded to pow2, min 8, `pvr_txr_load_ex` twiddled; no VQ, no mips,
  `pvr_mem_malloc` with 1024-id table (`gfx_pvr.c:92-103, 430-469`).
- Fog: PVR vertex fog, density from oargb alpha; fog-color register written
  directly (:549-560).
- AICA driver `src/audio/aica_synth.c`: replaces RSP render
  (`synthesis.c:328-345`); `AicaSynth_RefreshActive` per sub-update,
  `AicaSynth_Update` per frame (:518, :563). One `gNotes[]` → one of 64 channels
  (free stack :37-49, :332). Commands via `snd_sh4_to_aica` timestamp 0 (~1 frame
  latency, :307-330). 192-entry ARAM refcount/LRU cache, `spu_memload_sq`
  (:128-159). `fix_aica_freq_gap` for KOS firmware 10-bit overflow (:161-173).
  Long samples (>65534) stream through a dedicated channel with 4096-sample PCM16
  ARAM ring; SH-4 decodes Yamaha ADPCM ahead of a timer-estimated read head
  (:79-110, :343-439). Offline `tools/aica/transcode.py:292-342`, `ya2beam.c`
  (`Makefile:825-850`): VADPCM → beam-searched Yamaha ADPCM; fall back to PCM16/8 on
  low SNR or `FORCE_PCM_KEYS` (ADPCM cannot play above +1 octave, :73-81);
  over-length decimated 2× with `rate_scale`; Freeverb tail baked in (:237); loop
  points carried.
- libultra shim `src/pc/ultra_reimplementation.c` nearly empty: `osPiStartDma` =
  memcpy (:28); mesg queues no-ops (:35-72); VI no-ops; EEPROM → `/vmu/a1/mario64.rec`
  (:157-330). One game loop (`pc_main.c`) + one KOS audio thread (`PRIO_DEFAULT-1`,
  32 KB, :174) woken by vblank via `genwait` (:87-120).
  `mmu_page_map_static(0, 0x0C000000, 1M)` (:193). 30 fps lock = 2 vblanks
  (`gfx_dc.c:126`).
- Build: `$(KOS_CFLAGS) -Os -flto=auto -DTARGET_DC -DNDEBUG -fno-strict-aliasing
  -fwrapv -fipa-pta -fno-pic -fno-pie -DNON_MATCHING -DAVOID_UB -DNO_SEGMENTED_MEMORY`,
  `-O3` for gfx (`Makefile:549-571, 620, 990-991`). `make cdi`: `sh-elf-objcopy -R
  .stack -O binary sm64.elf sm64.bin`; `kos-cc loader.c -o loader.elf`;
  `mkdcdisc -n "Super Mario 64" -N -f sm64.bin -e loader.elf -o mario64.cdi`
  (:1023-1026). `loader.c` `fs_load`s `/pc` or `/cd/sm64.bin` → `arch_exec`.

## What wings64-dc takes
From Birdman64 (MIT, keep notice): the patch set minus rate/widescreen/64-bit
patches (keep endian, bitfield, UB); `native/src/pw64_audio_load.c`,
`pw64_rom_structs.c`, `pw64_layout_checks.c`; `segment_symbols.h` constants; the
flag set and defines; `pw64-platform` scheduler semantics ported to C on KOS;
`swap.rs` layout-validated swapping as `PW64_SWAP` in C; re-implement `mio0.rs` and
`tmem.rs` in C; treat `audio.md`, `gfx.md`, `formats.md`, `native-build.md` as spec.
From sm64-dc/sf64-dc (BSD gfx core; sf64-dc CC0): `gfx_retro_dc.c` + `gfx_pvr.c` +
`gl_fast_vert.h` + `gfx_cc.*` rebuilt for the F3D GBI with: physical-address OR
0x80000000, TMEM deswizzle in importers, TEXEL0-only for the mip-lerp combiner,
texture-cache reset on level load, cimg==zimg handling (z-clear + shadow volumes),
`uvCopyFrameBuf` handling. `aica_synth.c` + `tools/aica/*` keyed off alSyn PVoice
state. VMU EEPROM code, `loader.c`, cdi target.
