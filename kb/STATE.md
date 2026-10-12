# STATE — current numbers and the ranked queue

Updated 2026-10-12 (M4 done; visual passes 1-2 confirmed on hardware through run 11: water, horizon, landing target,
fades, title, photo album all good; the album menu is slow but accepted; next: queue below).

## Where things stand
| metric | value | evidence |
|---|---|---|
| milestone | **M4 done** (emulator gate + 10-min soak); hardware runs 1-11 (2026-10-11/12) | state-log |
| hardware frame rate (latest) | flight 45-51 fps (Rocket Belt Beginner; a hang glider test 36), title 37-38, results 33-62, album idle ~60 (DL 0.4 ms) | LOG005, LOG009 |
| hardware frame rate (before the fps pass) | flight 23-25, menus 13-19, title ~20, results 34-38 fps; CPU-bound | LOG000-001 |
| Flycast frame rate | title 57, menus 54-56, flight 56, results 58 fps | gate + `scripts/gfxstat.sh` |
| audio | AICA driver (src/dc/audio), whole pool resident (1,860,032 of 1,900,544 B), reverb off (DIVERGES); hardware 22.3-22.5 kHz, 0 misses, "sounds really good" | docs/audio-design.md |
| SD log on hardware | backported KOS 1c6398f SD stack (src/dc/kos_sd); WINGS64/LOGNNN.TXT + RECEIPT.TXT | docs/testing.md |
| hardware tools | `./run.sh hwdisc` (FPS monitor, SD log, DB_PROF, L+R graphics toggles), `tools/prof/decode_prof.py`, `scripts/gfxstat.sh` | docs/testing.md |
| last hardware disc | NAS wings64-dc/wings64-dc-20261012g-album-text.cdi (+ -dd, .elf; 6289338d-1012-2237): run 11, "it works" | NAS .src.json |
| boot | loads 5.32 MiB + AICA pool, then spins the drive down (`cd: spin down ok`); maintainer: "loading is much better" | LOG005 |
| toolchain | image `wings64-dc:ce773e5503b9`: GCC 14.3.0, KOS v2.2.2 `c23410b` (-m4-single-only), mkdcdisc v0.0.4 | `docker/pins.env` |
| decomp pin | `1a8558f`, **28 patches** (all `WINGS64_DC`-guarded; 026 fade frozen frame, 027 debug look-at-pad camera, 028 album persistent frame) | `patches/README.md` |
| I-cache | `.text.hot.gfx` 6,616 B (must stay well under 8 KB: 8.5 KB halved flight speed on hardware, run 6) | `build/wings64.map` |
| ROM fs slice | 5,582,256 B, crc32 `DAFCC147`, 1272 UV files | `tools/rom/slice_fs.py` |

## Frame-rate pass (docs/fps-plan.md; every change has a kill switch, default on)
| change | switch | effect |
|---|---|---|
| direct vertex emit into the DMA buffer | WINGS64_DIRECT_EMIT | Flycast title 28.7 -> 25.1 ms |
| outcodes, lazy XMTRX | WINGS64_OUTCODES, WINGS64_MVP_LAZY | Flycast flight 19.9 -> 18.3 |
| per-frame texture memo | WINGS64_TEX_MEMO | Flycast flight 18.3 -> 17.6 |
| rect colour cache, reciprocal UVs | WINGS64_RECT_FIT | pilot select 5.1 -> 3.5 ms |
| fsqrt sqrtf (bit-exact vs newlib) | WINGS64_FSQRT | profile item |
| I-cache hot section, per-frame extras outlined COLD | WINGS64_HOT_LAYOUT | hardware: 8.5 KB hot -> 38 fps flight, 6.5 KB -> 51 |
| decomp -O2 | WINGS64_DECOMP_O2 | small |
| fast texture conversion | WINGS64_FAST_TEXCONV | load spikes 501 -> 27 ms |
| **kernel idle spin blocks (patches/023)** | patch | **fps ~31 -> ~56 (Flycast); hardware flight 23-25 -> 45-53** |

## Graphics fixes (kill switch each; status from the maintainer's hardware runs)
| bug | cause | fix (switch) | hardware |
|---|---|---|---|
| column / trapezoid under vehicles | shadow volume pass 1 (cimg == zimg) dropped, pass 2 painted the volume | PVR cheap-shadow modifier volumes, OP/PT headers only (WINGS64_SHADOW_MOD) | ok (run 4) |
| horizon band through glider/HUD, pilots missing on pilot select | no-Z layers drawn before the scene land on top (PVR list order) | painter depth for pre-scene no-Z layers, post-scene no-Z to TR (WINGS64_LAYER_Z) | ok |
| sea affine / unreadable when flying over it | the painter layer gave the sea (a 3D plane) one constant depth; the PVR perspective-corrects with vertex depth | per-polygon painter depth keeping 1/w ratios, floors stacked per layer, floor 1e-8 (WINGS64_LAYER_Z; layer_depth) | ok (run 8) |
| sea blotchy up close | TEXEL0*TEXEL1 (one I4 wave texture at two scales) drawn as TEXEL0 | product baked 128x128 (WINGS64_TEX_PRODUCT) | ok |
| white line at the horizon, sea to the horizon | no far-plane clipping; Fast3D clips at z <= w, so on the N64 the sea stops short of the horizon | far plane in outcodes and clipper (WINGS64_FAR_CLIP) | "perfect" (run 9) |
| landing target cut off / striped | coplanar pad layers tie under GEQUAL 1/w; the RDP lets later draws win | per-state draw-order depth bias <= 0.4 % (WINGS64_Z_TIEBREAK); debug camera patches/027 | ok (run 6) |
| crash / Birdman white fade leaves a scene border | fades span SUBSCREEN; the port draws 3D unscissored | no-Z untextured quads over SUBSCREEN stretched to the screen (WINGS64_OVERLAY_FULL) | ok (run 7) |
| fade-out runs over black | func_8032D51C fades over the copied N64 frame buffer | last frame's lists replayed under the fade (patches/026, WINGS64_FB_FREEZE) | ok |
| title picture cut / inset | title composed inside SUBSCREEN | SUBSCREEN mapped to the screen inset 12 px, black outside (WINGS64_TITLE_FIT, TITLE_MARGIN) | "looks good" (run 7) |
| photo album black | the album draws each photo once and relies on the N64 frame buffer persisting | front-buffer readback as first layer (patches/028, WINGS64_FB_PERSIST) + clip to small scissor/viewport (WINGS64_SCISSOR) | works (run 11) |
| album blur / shrink / flicker / hangs | KOS scaler 0x401 (composite) smooths and shrinks every frame-buffer write; whole-screen re-captures compounded it; run-time SCALER_CFG writes hung the PVR | captures copy only rectangles drawn in the last two frames; text strips once per layout; cursor bars apart; TR x/y hash policy; quarter-texel sampling; scaler untouched | ok (run 11); menu slow, accepted |
| drive loud / strained | disc spun flat out all session | CMD_STOP after the boot loads (WINGS64_CD_SPINDOWN) | ok (run 7) |
| hardware bisect aid | | hwdisc: hold L+R + Up/Down/Left/Right/A = modifier/layer/product/tiebreak/dxt off (DB_GFX_TOGGLES) | used in run 7 |

## Hardware runs (details in state-log.md)
| run | disc | outcome |
|---|---|---|
| 1-2 | 0a43221 | boots, audio good, SD log; 15-25 fps |
| 3 | 462f919-1011-2314 | ~49 fps flight after the fps pass; Class A Gyrocopter overview hang (not reproduced since) |
| 4 | 123d29e-1012-0123 | visual fixes hold; water wrong, pop-in on fast turns |
| 5 | 123d29e (LOG003) | flight 47-56 fps, no hang |
| 6 | 6289338d-1012-1740 | target fixed; water, title overshoot, drive noise, album slow/blurry; I-cache regression 38 fps |
| 7 | 6289338d-1012-1816 | loading, title, target, fades good; water = painter layer (toggle bisect); album blur/shrink |
| 8 | 6289338d-1012-2102 | water better, horizon white line; album flicker, hang on exit (scaler restore) |
| 9 | 6289338d-1012-2148 | horizon perfect; album hang (scaler write) |
| 10 | 6289338d-1012-2216 | no crash; album still blurs on cursor moves (text union) |
| 11 | 6289338d-1012-2237 | album works; menu laggy, accepted |

## Ranked queue
1. **Album menu speed** (run 11, accepted for now): the thumbnail fill-in phase runs at 2-4 fps (the game's own 0.1 s
   wait per slot, 3D renders, rectangle captures up to 151 ms once). Ideas in kb/levers.md L16.
2. **Pop-in on fast turns** (run 4, low priority): not reported in runs 7-11. `ta opb-oom` rises exactly 15 per 30
   frames in every state, even the near-empty album, so it is not list exhaustion; identify the event first (L17).
   Suspects otherwise: decomp culling a frame behind (patches/020), WINGS64_OUTCODES, WINGS64_TEX_MEMO.
3. **Title fps** 37-38 (run 5: 46): title DL 17 ms on hardware; check the title-fit border/overlay cost.
4. **Class A Gyrocopter overview hang** (run 3): never reproduced; watch `stall:` lines.
5. "uv: RSP timeout on g ... Recovered from a bad display list" at menu/level loads (game watchdog on long loads; harmless so far).
6. Audio fidelity (M5): reverb via AICA DSP, sub-frame parameter timing, PCM8 for waves that sound bad.
7. Later levers (kb/levers.md): LTO (L14), profile-guided hot/cold layout (L15).
8. M5 parity (all vehicles/levels), M6 (fps on hardware), M7 release.
