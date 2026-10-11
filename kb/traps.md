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

## Paid for in M2 (2026-10-11)
- KOS genwait wakes from an IRQ only mark threads ready; the switch waits for the next tick. The shim sets
  thd_set_hz(1000) and yields after thread-context sends to approximate N64 preemption.
- osPiReadIo callers treat the word as bytes: copy it raw (big-endian N64 register image), not numerically.
- Physical DC RAM is 0x0Cxxxxxx, which reads as F3D segment 12: addresses whose segment has no base are
  physical (ram() in gfx_f3d.c). PW64 only sets segment 0 = 0.
- PW64's F3D texrect is E4 + B3 (s,t) + B2 (dsdx,dtdy), not gbi.h's B4/B3 (geometry.c/sprite.c redefine).
- UVBT/font tiles declare more rows than were loaded into TMEM; clamp to loaded bytes / line.
- RDP rectangles have no shade: SHADE inputs read 0 (a white shade painted rect borders white).
- Tracing with many db_log lines per frame slows the emulated game enough to shift timed autoplay input;
  gate scripts use wide gaps.
- Screenshots/videos render ROM assets: never commit them (rom_leak_check rejects images).
- Official macOS Flycast crashes here; see docs/flycast-macos.md (`./run.sh play` = VNC workaround).

## Paid for in M3 (2026-10-11)
- KOS v2.2.2 `pvr_fog_vertex_color()` is `assert_msg(0, "not implemented")`: it aborts, the console
  reboots, and under Flycast's HLE BIOS the second boot panics ("couldn't find a runnable thread").
  Write PVR_FOG_VERTEX_COLOR (0xA05F80B4) directly. Vertex fog blends toward that register, not the table colour.
- N64 `Mtx` is two u16 arrays (Mtx_u i[4][4], f[4][4]) written halfword by halfword: on little-endian SH-4
  element e is halfword e. Decoding it as big-endian word halves swaps element pairs (title models mis-scaled).
- **Arrays declared shorter than the game uses are UB GCC acts on.** The camera struct declares the
  ground view polygon `Vec3F unk328[1]` and the game writes five points; GCC 14 cut the copy/read
  loops to one iteration, the polygon read back as zeros and the island terrain was never drawn
  (only the far sea ring). Fix: patches/020 sizes the arrays to their real extent; decomp TUs also
  build with `-fno-aggressive-loop-optimizations`. Symptom pattern: geometry the game culls itself.
- **Combiner alpha without TEXEL0 must not use texture alpha.** The sky cloud strip (CVG_X_ALPHA, PT
  list) has combiner alpha 1; with `MODULATEALPHA` + texture alpha its transparent texels punched a
  black band through the sky. `txr_alpha` follows the fitted alpha's texel term.
- **Controller polls stop for seconds after a menu change** (asset loads): an autoplay stick step of
  300 ms inside that window is lost. Gate scripts use state-relative steps re-armed after load gaps
  (`s<state>+ms`, src/dc/autoplay.c), and every press lasts at least 3 polls.

## SD card logging (carried over from OpenCrossing-Dreamcast hardware runs, 2026-10-11)
- `O_APPEND` on fs_fat: 0x08 lands inside `O_MODE_MASK`; the open succeeds and every write fails
  with EBADF. Open once with `O_TRUNC`, hold the fd, write sequentially.
- fs_fat postpones block/inode writes: without `fs_fat_sync`/unmount the file is empty or short.
- The serial-port adapter turns SCIF into SPI: nothing may write the console while the card is up
  (`dbgio_disable` before the probe, `dbgio_enable` on failure or switch-off).
- SCI probe waits for RDRF with no timeout: no card = permanent hang. Default SCIF only.
- Muting the console at `main()` changed OpenCrossing's boot timing and stopped it booting on
  hardware: arm the card after the game loop runs.
- MBR FAT32 with 4 KB clusters; superfloppy cards don't enumerate.

## Port settings menu (2026-10-11)
- **uvFontGenDlist cannot run twice in a frame.** It restarts the font sprite display-list cursor at
  base + messages-so-far, so a second call overwrites the commands of text already emitted that frame.
  Extra text after the game's own goes through `uvFontOverlayStr_dc` (patches/021), which continues
  from `rsp_dl_next`.
- **Font 6 has two alphabets.** Its STRG holds a thin set first and the bold set the text tables use
  second ("Option" = 0x78... is the second set). `uvFontPrintStr` picks the first match, the thin set,
  so menu-looking strings must be built from the second set (`src/dc/settings.c` glyphs()).
- **SD log ON in Flycast:** the SCIF probe can stall the main monitor thread (the gate markers come from it),
  and once it has armed the serial console stays silent. Never leave the SD row ON in an emulator run.

## Paid for in M4 audio (2026-10-11)
- **VADPCM loop state = the decoded frame holding loopstart** (samples ceil-to-16 of loopstart minus 16 ..
  that frame end), bit-exact with tools/audio/vadpcm.py. Use it as the decoder oracle.
- **Looped libaudio waves never play past loop end** (count -1): truncating there shrank the pool enough
  to keep it all resident (1.77 of 1.81 MiB AICA sample RAM). KOS sample RAM is 0x030000..0x200000.
- **The N64 resampler clamps pitch at MAX_RATIO 1.99996**: no voice exceeds +1 octave over its wave rate,
  so the AICA ADPCM ceiling needs no offline pitch analysis.
- **alAudioFrame must report a non-empty command list.** audio_manager.c only queues the previous frame's
  AI buffer after a task; *cmdLen 0 freezes the DAC pacing loop.
- **osAiGetLength is the playing buffer only**, not the queued one; returning the whole backlog drives
  frameSamples to the minimum and the DAC runs dry every frame.
- **The audio task must not wait for the scheduler**: the port's scheduler thread interprets display lists
  on the CPU, so a routed audio task blocks __amMain past retraces (7% frames lost). patches/022.
- **A stub AI (osAiGetLength = 0) runs the audio clock 1.32x fast** (484 samples per 368-sample frame):
  anything timed by music (uvaSeqStop's wait for note releases) got shorter in M1-M3 than on an N64.
- KOS firmware has no volume envelopes (env reg 0x1f): libaudio ramps are SH-4 UPDATE(SET_VOL) steps;
  AICA `vol` is linear amplitude (the ARM maps it through a log table), libaudio squares its volume first.
- `snd_sh4_to_aica` never checks for queue space (32 KiB); one channel packet is 96 B, so a full 32-voice
  frame is ~3 KiB. Batch with snd_sh4_to_aica_stop/start so the ARM sees a frame at once.
- Yamaha ADPCM on saw-like waves with hard edges sits near 10 dB SNR (step adaptation lags the jump);
  a beam search buys ~1 dB. PCM8 is the fix, budget permitting.

## Paid for on the first hardware runs (2026-10-11)
- **KOS v2.2.2's SD/SCIF-SPI driver never brings a card up on the console** (stuck "probing", card in the
  adapter). KOS 1c6398f's sd.c + scif-spi.c (backported in src/dc/kos_sd) work first time on the same card.
  Use 8.3 upper-case names on the card. The console is muted during bring-up: show SD state on screen.
- **Flycast frame rates are not hardware frame rates**: ~30 fps in the air in Flycast, 15-20 on the console.
