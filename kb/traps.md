# traps — gotchas already paid for by the reference ports

## Compiler / decomp
- The decomp defines `__attribute__(x)` empty in `macros.h`; this silently drops
  `aligned(8)` from matrix types. Shadow `macros.h` with `#include_next` then
  `#undef __attribute__`, and `_Static_assert(_Alignof(shz_mat4x4_t) == 8)`.
- `gbi.h` guards its whole body on `_LANGUAGE_C` — define it.
- IDO chars are unsigned → `-funsigned-char`. VR4300 has no FMA → `-ffp-contract=off`
  in decomp TUs (sh4zam TUs may use `-ffp-contract=fast`).
- clang turned `hangGliderMovementFrame`'s uninitialised `sp5B` into a trap →
  `-ftrivial-auto-var-init=zero`.
- Real UB to patch (Birdman64): `audio_manager.c` block-scope `customFxParams`
  (make static), `test_summary.c` `sp5C[3]` overrun, `system.c` `gSiContStatus[4]`,
  `demo.c` RHDR stride 24, `synthesizer.c` ALParam pool stride (`sizeof` on SH-4).
- `va_start` on promoted types at `chan.c:360`, `audio_emitter.c:174`, `code_1050.c:43`.
- `_uvMediaCopy` contains a deliberate fault `*(u16*)1 = 0` — patch out.
- Header deps are not tracked by the reference Makefiles → `.cflags` stamp +
  `./run.sh clean` after header edits (or generate `-MMD` deps; preferred).

## Endianness
- IDO packs bitfields MSB-first, GCC LSB-first: reverse `uv_filesystem.h` bitfields
  and read `setimg.cmd` as `w0>>24` (`texture.c:807`).
- UVTX images are stored in TMEM order (odd rows 32-bit-swapped) and loaded with
  `LoadBlock dxt=0` → deswizzle on import or the texture is garbage.
- `code_8170.c` rewrites texels as u16 in place (env tint) — swap-aware.
- Audio bank (`alBnkfNew`) relocates offsets in place → use a field-wise
  deserialiser instead.

## Memory map
- DC RAM is 0x8C000000-based: bit 31 set, so the game's "bit31 clear ⇒ ROM" test
  still works; P1 0x80000000 is boot ROM, so every `0x80xxxxxx` literal must go to
  an arena (39 sites listed in `docs/reference/birdman64-sm64dc.md`).
- KOS keeps two copies of the TA vertex buffer in VRAM (512 KB → 1 MB).
- `memalign(32)` for anything written with `movca.l`/store queues or read by
  GD-ROM DMA; `shz_dcache_alloc_line` needs the whole 32-byte line owned.

## PVR
- `pvr_wait_render_done()` before any VRAM upload/free (ssb64-dc `dc_pvr_vram_fence`).
- `PVR_TXRFMT_STRIDE` bug in some KOS versions (DCA3 static_assert) — avoid stride textures.
- PT alpha reference must be set (`PVR_SET(PVR_PT_ALPHA_REF, 0x80)` or 0xA05F811C).
- KOS `vid_set_mode(DM_640x480, PM_RGB565)` picks 480p on VGA, 480i otherwise; never 50 Hz.
- Texture cache keyed by RAM address must be wiped on level load (PW64 reuses its heap).

## Audio
- KOS AICA firmware has a 10-bit frequency overflow (`fix_aica_freq_gap`).
- Yamaha ADPCM cannot play above +1 octave → PCM fallback for high-pitched SFX.
- SD/IDE loading starves the mixer on real hardware (sf64-dc) — CD-R is the target.
- PW64's AI lead is only ~100 samples (~4.5 ms) → do not emulate aspMain timing; drive AICA directly.

## Toolchain / disc
- `arch_exec()` was broken in a KOS version (mk64-dc loader workaround) — we do not
  use a loader stub; mkdcdisc takes the ELF directly.
- mkdcdisc `-N` (no padding) is for emulators/ODE; CD-R wants padding. Late VA2
  consoles may need `-L data-data`.
- Flycast has no headless/auto-exit flag; serial console via
  `-config config:Debug.SerialConsoleEnabled=yes`.
- GCC 15.2 was tried and abandoned by ssb64-dc (reason unrecorded); 14.3 proven by sf64-dc.
- sm64-dc/sf64-dc use OLD sh4zam names (`shz_xmtrx_trans_vec4`, `shz_fast_invf`,
  `shz_matrix_4x4_t`) that no longer exist — translate, never copy.

## Paid for in M0 (2026-10-10)
- KOS v2.2.2 ships `utils/dc-chain` (not `kos-chain`); profiles 13.4.0/14.3.0/15.1.0; no 13.2.0.
- colima has no buildx: legacy `docker build` (no `--progress`, stages run sequentially). A stale local
  `debian:bookworm` gave "content digest … not found" → `docker pull debian:bookworm`.
- macOS `/bin/bash` is 3.2: `"${arr[@]}"` on an empty array under `set -u` aborts → use `${arr[@]+"${arr[@]}"}`.
- KOS v2.2.2 has no `PVR_PT_ALPHA_REF` name; register offset is 0x11C (`src/dc/dcpvr.h`).
- KOS_CFLAGS carries `-fno-builtin`; the sh4zam library objects re-enable `-fbuiltin` after it.
- mkdcdisc `-d disc` adds a `disc/` directory on the CD; `-D disc` stages its contents at `/cd/`.
- ISO primary names are 8.3 (`wings64.meta` → `WINGS64.MET`); Joliet/RR carry the long name, but keep
  disc files 8.3-safe so `/cd` lookups never depend on extension handling.
- Padded CD-R CDI is ~705 MB of raw 2352-byte sectors (fills toward an 80-min disc): use 80-min CD-R.
- The `shz_`/`SHZ_` prefixes are reserved for sh4zam: the name lint flags port identifiers using them.
- Flycast 2.6 on this Mac (Darwin 27) aborts in `sh4/dyna/driver.cpp:347` "Verify Failed &mem_b[0] == …"
  for every image, dynarec on or off (2.7 too). Fix: Flycast v2.7 built from source on Linux in
  `docker/emu/` under Xvfb + Mesa llvmpipe works (`./run.sh smoke`). `--user` without a writable
  $HOME/.config only produces harmless "cannot save emu.cfg / nvmem" warnings.
- `mallinfo().fordblks` is free space inside the malloc arena, not free RAM (M0 screen shows "free 10 KB").

## Paid for in M1 (2026-10-10)
- GCC 14 makes int<->pointer conversions errors; the decomp relies on them. Decomp TUs only get
  `-Wno-int-conversion -Wno-incompatible-pointer-types` (lossless on ILP32).
- `--gc-sections` silently drops a decomp that nothing reachable references, and undefined symbols in
  dropped sections are not reported. `main.c` reads `bootproc`'s address so the link resolves the game.
- sh-elf prefixes C symbols with `_`: `--defsym=_kernel_TEXT_START=…` (ARM host: no underscore).
- Adding a shadow header does not trigger rebuilds via -MMD; the shadow list is in the .cflags stamp.
- objdump decodes SH literal pools as instructions (fake `fmac`); purity checks scan GCC `-S` output.
- Birdman64 patches have wrong `@@` line counts (hand-edited): GNU patch/git apply reject 10 of 76.
- The decomp has no `src/libultra/gu` (PLAN M1 listed it); the game uses no gu* beyond sqrtf.
- Host tests cannot run on the Mac (LP64, no -m32): 32-bit ARM cross + qemu-arm in the container.
  ARM EABI aligns 8-byte types to 8 (SH-4: 4), so layout asserts are compiled for SH-4 too.
- `uvDbg_802339B0` colours 0x26 vertices across four arrays IDO placed back to back (patches/010).
