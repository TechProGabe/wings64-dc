# Web prior-art brief (2026-10-10)

## PW64 decomp
- gcsmith/Pilotwings64Decomp — MIT, created 2025-12-19, 380 commits, last push 2026-09-28. US 100.00% code (838 kB code, 8.22 MB data) @ 1a8558f. JP target in progress.
- Toolchain: splat (compiler IDO, gfx_ucode f3d), ido-static-recomp IDO 5.3 -O2 -mips2 -32. tools/Torch submodule NOT wired. `make extract` = splat only (asm + raw .bin). No NON_MATCHING/modern-GCC target; RECOMP_BUILD define exists (include/macros.h). libultra 2.0I, full source in src/libultra (docs/flib_2.0I.txt).
- ROM map: code+filesystem end ≈ 0x618B6C; ~1.9 MB padding.
- Microcode: STOCK SGI — src/rsp/fast3d.s, f3dex.s (gspF3DEX_fifo), aspMain.s, rspboot.s. Audio: stock libultra alSyn/sequence player, OUTPUT_RATE 22050 (include/kernel/uv_audio.h), NUM_DMA_BUFFERS 48, FRAME_LAG 2 (src/kernel/audio_manager.c).
- UVxx filesystem: docs/pilotwings64_filesystem.md. Index at ROM 0x0DE720 (UVRM::TABL, MIO0 list of (tag,size)); FS at 0x0DF5B0, each FORM/len/tag. UVMD model, UVTX texture (MIO0 COMM), UVCT contour (MIO0 terrain geometry), UVTR terra (tile grid + matrices), UVLV level, UVEN env (screen + fog RGBA), UVSX sound bank (.CTL/.TBL), UVSQ, UVSY, UVAN, UVBT, UVFT, UVTI/UVTP, user files UPWL/UPWT/3VUE/ADAT. UVMD layout "TBD" in doc. Engine "UltraVision" (Paradigm).
- Pre-decomp tooling: queueRAM/pilotwings_64 (fs dump, n64split cfg, ImHex pattern); noclip.website wiki "Pilotwings-64-Research-&-Development", parser src/Pilotwings64/Scenes.ts.
- Other ports: gcsmith/Pilotwings64Recomp (N64Recomp+RT64, WIP); danielgomesvieira2000/pilotwings-64-recomp; NoahVl/Birdman64 (decomp C compiled with Zig/clang vs Rust libultra: pw64-platform coroutine threads/PI/SI/AI/VI/mesg; pw64-gfx Fast3D+RDP HLE wgpu; pw64-audio aspMain HLE; patches in crates/pw64-game/patches/; v1.0 2026-10-03, MIT).
- Video: OS_VI_NTSC_LAN1 320x240 16-bit, dither filter on (src/kernel/system.c). No Expansion Pak.
- RDP: heavy 2-cycle (gsDPSetCycleType G_CYC_2CYCLE in init DL and uvGfxStateDraw); G_RM_PASS, G_RM_ZB_XLU_SURF2, G_RM_OPA_SURF/_SURF2; Z always on (G_ZBUFFER|G_SHADE|G_CULL_BACK|G_SHADING_SMOOTH); vertex fog gSPFogPosition(factor*1000,1000) via uvGfxSetFogFactor; G_TEXTURE_GEN|G_LIGHTING env-map path; G_TL_TILE LOD (src/kernel/graphics.c).
- Terrain: UVTR grid tiles -> MIO0 UVCT meshes with per-tile matrices (src/kernel/terrain.c). Per-frame tri counts UNVERIFIED.
- N64 fps: ~30 typical, dips 15-20; menus 60.
- Threads: _uvScCreateScheduler (custom sched, M_GFXTASK/M_AUDTASK); kernel pri 0xC, render 0xFA, app 0xA, PI manager.

## N64->DC prior art
- jnmartin84/sm64-dc (branch ootpvr): src/pc/gfx/gfx_retro_dc.c = gfx_pc.c interpreter rewritten for DC (sh4zam.h, 320x240 virtual coords); gfx_pvr.c = RAW PVR backend (pvr_dr store queues, pre-compiled pvr_poly_hdr_t bit-patched, OP/PT/TR from othermode, depth from RDP bits, G_FOG->vertex fog, ARGB1555/4444 non-twiddled). GLdc tried 2024, abandoned. Audio src/audio/aica_synth.c: 64 AICA channels fed from gNotes[] each frame via KOS SH4->AICA cmd queue; samples pre-transcoded to Yamaha ADPCM into ARAM; >65534-sample go through SH4 PCM ring streamer. libultra shim src/pc/ultra_reimplementation.c (mesg queues, VI swap, PI DMA, EEPROM -> /vmu/a1/...). pc_main.c: game loop on main KOS thread, thd_create_ex audio thread, vblank_handler_add, thd_set_hz(300). Disc: loader.elf fs_load /cd/sm64.bin + arch_exec; `mkdcdisc -n "Super Mario 64" -N -f sm64.bin -e loader.elf -o mario64.cdi`.
- mk64-dc: "30 fps 4 player with music", 24-bit fb, sh-elf-gcc 13/14/15, macOS 15.5 OK.
- sf64-dc: 480p, JITs N64 display lists to SH4 via SH4ZAM, ~95% Oct 2025.

## Toolchain 2026
- KallistiOS v2.2.2 (2026-04-18). v2.2.0: libpthread, default m4-single FP ABI, CD IRQ DMA. GCC 9-15 supported. master pushed 2026-10-07.
- dc-chain renamed kos-chain (utils/kos-chain, 2026-02-07). stable = GCC 15.2.0 / binutils 2.45.1 / newlib 4.6.0; legacy = GCC 13.2.0; 16.2.0 profile exists. macOS Intel+Apple Silicon via Homebrew (utils/kos-chain/doc/macos.md).
- mkdcdisc gitlab.com/simulant/mkdcdisc v0.0.4 (2026-09-20), meson. Embeds IP.BIN, scrambles ELF itself. Flags: -e ELF | -b/-B BIN | -t ISO; -o out.cdi|.gdi|.mds|.nrg; -n title; -d dir; -f file; -N disable data-track padding; -L audio-data|data-data|merged-data (data-data boots late VA2 consoles that reject Audio/Data); -c CDDA; -p custom IP.BIN; -i/-m license image.
- GLdc gitlab.com/simulant/GLdc maintained (2026-09-13), branch sh4zam. jnmartin84 off it for N64 ports.
- Flycast CLI: `flycast [-config section:key=value] <path>`; .cdi/.gdi/.chd load; .elf direct with bios.UseReios=yes; `-config config:Debug.SerialConsoleEnabled=yes`. No headless/auto-exit flag.
