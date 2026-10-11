# Architecture (decided)

## Directory layout to create (M0–M2)
```
run.sh                   dispatcher: setup | build | disc [--emu] | release | test [--ci] | run-hw | decomp-refresh | clean
scripts/                 ctr.sh (container exec), dc_env.sh (flags), build_target.sh, make_cdi.sh, run_hw.sh,
                         fetch_decomp.sh (+ apply patches), test_host.sh, test_tree.sh, bump_pins.sh
docker/                  Dockerfile (kos-chain → KOS → mkdcdisc → arm-eabi), pins.env, fetch-src.sh
external/sh4zam/         git submodule → TechProGabe/sh4zam (pinned)
external/pw64-decomp/    gitignored: decomp at DECOMP_REV with patches/ applied
patches/                 NNN-<reason>.patch + NNN-<reason>.md (why, origin: Birdman64/own)
src/dc/decomp/           shadow headers: PR/os.h PR/ultratypes.h macros.h segment_symbols.h libc/*
src/dc/ultra/            os shim: thread.c mesg.c event.c timer.c pi.c (ROM-in-RAM) vi.c sp.c ai.c si.c eeprom.c mio0.c
src/dc/gfx/              gfx_f3d.c (interpreter) gfx_pvr.c (backend) gfx_tex.c (import/deswizzle/cache) gfx_cc.c (combiner) clip.c mtx.h
src/dc/audio/            aica_synth.c (alSyn voice API + alAudioFrame → AICA channels) aica_drv.c (pool upload, KOS cmd queue)
src/dc/rom/              romfs.c (fs image load, filetable walk, swap helpers pw64_swap.c)
src/dc/input.c  save.c  main.c  db.h  db.c  dcpvr.h  stackguard.c
src/game/                Makefile: OBJS from external/pw64-decomp (app, kernel, libultra/{audio,sp,gu}) + src/dc
tools/rom/slice_fs.py    baserom.z64 → disc/wings64.fs (SHA-1 gated)
tools/audio/             transcode.py (ctl walk, vadpcm.py decode, yamaha.py encode) → wings64.aud; test_audio.py
tools/check/             rom_leak_check.py shz_names_check.py diverges_check.py purity (fpu_check.py) vramfence_check.py
src/dc/test/             host build: hoststubs/ (kos.h, dc/pvr.h, dc/maple.h…), tests, sh4zam SW backend
disc/                    gitignored outputs: wings64.fs adpcm_pool.bin sample_table.bin disc.id icon.vmi
build/                   gitignored
```

## Data flow
```
baserom.z64 ──tools/rom/slice_fs.py──► disc/wings64.fs (ROM 0xDE720..0x6314D0, byte-identical)
baserom.z64 ──tools/audio/transcode.py► disc/wings64.aud (wave table + Yamaha ADPCM pool, loop points)
decomp C + patches + shadows ──kos-cc──► src/game objects ─┐
src/dc/* ──────────────────────────────────────────────────┼─► build/wings64.elf ──mkdcdisc -d disc/──► build/wings64.cdi
external/sh4zam (header-only + libsh4zam.a) ───────────────┘
```

Runtime:
```
boot: KOS init → pvr_init → asset root probe (/pc, /cd/disc.id, /rd) → read wings64.fs into
      romfs (memalign 32) → upload adpcm_pool to AICA RAM → start game threads (ultra/thread.c)
frame: app thread builds Gfx DL (game code, unmodified) → osSpTaskStart → render thread:
      gfx_f3d.c interprets DL: gSPMatrix → XMTRX (sh4zam), gSPVertex → transform/light/fog/clip,
      G_TRI1 → OP via DR store queues, PT/TR buffered → pvr_scene_finish → post SP/DP done mesg
      → VI retrace event from vblank handler → game's scheduler advances
audio: audio thread (vblank-woken) runs ALCSPlayer/ALSndPlayer (decomp C) → alSyn voice calls →
      aica_voice.c → snd_sh4_to_aica channel commands (pitch/vol/pan/start/stop)
I/O:  _uvMediaCopy/_uvDMA → shz_memcpy from romfs → PW64_SWAP at the enumerated sites
save: osEeprom* → 0x208-byte image ↔ VMU file (dirty flag, writer thread, notice)
```

## Address model
- N64 KSEG0 `0x80xxxxxx` literals → arena pointers (patch). `OS_PHYSICAL_TO_K0` /
  `K0_TO_PHYS` kept: on SH-4 they are `| 0x80000000` / `& 0x1FFFFFFF`, which map
  P1 ↔ physical correctly. Gfx `w1` addresses translate with one OR.
- ROM offsets (bit 31 clear) index `romfs` at `rom_addr - 0xDE720`; anything below
  `0xDE720` (code) is a bug → `abort` in debug builds.
- Segmented addresses: Fast3D `MOVEWORD SEGMENT` table honoured in the interpreter.

## Threads and priorities (KOS: lower number = higher priority)
| libultra thread | N64 pri | KOS pri | notes |
|---|---|---|---|
| render | 0xFA | 5 | interprets DL; owns XMTRX during DL execution |
| audio | (libultra audio) | 6 | vblank-woken, bounded catch-up |
| scheduler | sched.c | 7 | message pump |
| kernel | 0xC | 8 | |
| app | 0xA | 9 | game logic |
| PI/VI managers | — | n/a | folded into pi.c/vi.c (synchronous + vblank handler) |
`thd_set_hz` low (e.g. 100) to minimise slicing; `osYieldThread` → `thd_pass`.
Fallback if semantics break: coroutine shim (ssb64-dc `sysshim.c`).

## Rendering details (Phase A)
- Vertex: 16-slot cache (Fast3D), 32-byte `LoadedVertex` {sx, sy, invw, w, argb, oargb, u, v, flags}.
- Transform: XMTRX = Pwxyz · Screen · P · MV rebuilt on matrix change (`sh4zam-mandate.md` §N.3);
  per vertex one FTRV + `shz_invf_fsrra` (w>0 branch) — exact in physics only, this is render.
- Lighting: lights to object space per matrix change (`shz_mat4x4_transform_vec3_transpose`),
  per vertex `shz_vec3_dot3` (FIPR). Texgen (`G_TEXTURE_GEN`) via lookat vectors same way.
- Clip: near plane only (Sutherland–Hodgman), guard band for x/y; `pvr_set_zclip(0)`.
- Combiner: 13 known modes evaluated per vertex into argb + oargb (offset color always on);
  TEXEL1 → TEXEL0; `LOD_FRAC` lerp collapses to TEXEL0. Unknown combiner → `DB` log + flat.
- Lists: `FORCE_BL` → TR; `CVG_X_ALPHA`/alpha-compare cutouts → PT; else OP. Blend from blender bits.
- Depth: PVR z = invw, `DEPTHCMP_GEQUAL`; decal ×1.003; `ZALWAYS` for overlays; ortho via incrementing z.
- Fog: PVR vertex fog, density from `gSPFogPosition` (fm = 128000/(max−min)), color register direct.
- Textures: import → TMEM deswizzle → ARGB1555 (RGBA16, IA4) / ARGB4444 (I4, I8, IA8, IA16, RGBA32)
  → pad to pow2 (min 8) → `pvr_txr_load_ex` twiddled; 1024-entry address-keyed cache; reset on level load.
- Special: `cimg==zimg` FILLRECT = Z clear → no-op (PVR clears per frame); shadow volumes → PT depth-only or drop (DIVERGES);
  TEXRECT/FILLRECT → screen-space sprites in PT/TR; `uvCopyFrameBuf` → render-to-texture or DIVERGES.
- Output: 640x480 RGB565, game space 320x240 ×2; `pvr_init` OPB {16,0,16,0,16}, 512 KB vertex buffer.

## Audio details
- Pool: all 164 waves Yamaha ADPCM (PCM16/8 fallback where pitch > +1 octave or SNR low), loop points carried,
  table {offset, length, loop_start, loop_end, fmt, base_rate}. Resident in AICA RAM; `spu_memload_sq`.
- Voice: `alSynAllocVoice/StartVoice/StopVoice/SetPitch/SetVol/SetPan/SetFXMix` → channel state → one
  `snd_sh4_to_aica` packet per changed channel per frame; `fix_aica_freq_gap`.
- Players: decomp `src/libultra/audio` sequence player + sound player unchanged; `alAudioFrame` replaced
  by the voice flush. Output rate semantics: 22050 base → channel pitch.
