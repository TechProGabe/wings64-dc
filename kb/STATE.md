# STATE — current numbers and the ranked queue

Updated 2026-10-11 (M4 done; two hardware runs; frame-rate pass: Flycast ~55-57 fps everywhere after
patches/023; next: confirm on hardware, then the visual bugs).

## Where things stand
| metric | value | evidence |
|---|---|---|
| milestone | **M4 done** (emulator gate + 10-min soak); first hardware runs 2026-10-11 (boots, audio good, SD log works) | state-log 2026-10-11 |
| hardware frame rate (before the fps pass) | flight 23-25, menus 13-19, title ~20, results 34-38 fps; PVR wait 7 us, so CPU-bound | hw SD logs, state-log |
| hardware DL interpret (before the fps pass, median) | flight 25-27 ms (max 75), file menu 43-45, class select 41-43, title 30-32 | `gfx: wait/dl/finish` |
| Flycast frame rate (after the fps pass) | title 57, menus 54-56, flight 56, results 58 fps (was 22-33); DL flight 9.9 ms, menus 12.0-12.3, title 11.4 | gate + `scripts/gfxstat.sh`, build 508c460+023 |
| Flycast CPU split (flight, after 023) | scheduler/interpreter 69%, game logic 2%, idle spin 0% (was 55-62%) | `cpu:` log lines |
| menu flow (Flycast gate) | Rocket Belt flight reached at 50 s (was 108 s): load stalls shrank with patch 023 | `state:` lines |
| game speed | wall-clock based: Rocket Belt Beginner flight 19.75 s at 57 fps vs 21 s at 32 fps | `state:` lines |
| audio | AICA driver (src/dc/audio), whole pool resident (1,860,032 of 1,900,544 B), reverb off (DIVERGES); hardware 22.3-22.5 kHz, 0 misses, "sounds really good" | docs/audio-design.md, hw log |
| SD log on hardware | works with the backported KOS 1c6398f SD stack (src/dc/kos_sd); 8.3 names WINGS64/LOGNNN.TXT; state under the FPS counter | state-log 2026-10-11 |
| hardware tools | `./run.sh hwdisc` (FPS monitor + SD log + DB_PROF on), profiler dumps per game state and on L+R+Start, `tools/prof/decode_prof.py`, `scripts/gfxstat.sh` | docs/testing.md |
| last hardware disc | NAS wings64-dc/wings64-dc-20261011d-fps.cdi (462f919-1011-2314: fps pass + patches/023; FPS, SD log, profiler on); not yet run on hardware | NAS .src.json |
| toolchain | image `wings64-dc:ce773e5503b9`: GCC 14.3.0, KOS v2.2.2 `c23410b` (-m4-single-only), mkdcdisc v0.0.4 | `docker/pins.env` |
| decomp pin | `1a8558f`, **23 patches** (all `WINGS64_DC`-guarded; 022 audio task, 023 idle spin) | `patches/README.md` |
| build | decomp at -O2 (WINGS64_DECOMP_O2), text 947,540 B; purity OK (184 decomp TUs) | `kos-size`, `make purity` |
| ROM fs slice | 5,582,256 B, crc32 `DAFCC147`, 1272 UV files | `tools/rom/slice_fs.py` |
| boot load (emulator) | 5.32 MiB in ~2.9 s + AICA pool ~2 s | serial log |

## Frame-rate pass (docs/fps-plan.md; every change has a kill switch, default on)
| change | switch | Flycast effect |
|---|---|---|
| direct vertex emit into the DMA buffer | WINGS64_DIRECT_EMIT | title 28.7 -> 25.1 ms, menus 28.3 -> 25.3 |
| outcodes, lazy XMTRX | WINGS64_OUTCODES, WINGS64_MVP_LAZY | flight 19.9 -> 18.3 |
| per-frame texture memo | WINGS64_TEX_MEMO | flight 18.3 -> 17.6 |
| rect colour cache, reciprocal UVs | WINGS64_RECT_FIT | pilot select 5.1 -> 3.5, results 10.4 -> 8.0 |
| fsqrt sqrtf (bit-exact vs newlib) | WINGS64_FSQRT | profile item, hardware gain |
| I-cache hot section (5,184 B contiguous) | WINGS64_HOT_LAYOUT | title 23.1 -> 19.8; cache effect hardware-only |
| decomp -O2 | WINGS64_DECOMP_O2 | small in Flycast |
| fast texture conversion (Haiku agent) | WINGS64_FAST_TEXCONV | load spikes 501 -> 27 ms |
| **kernel idle spin blocks (patches/023)** | patch (N64 build unchanged) | **fps ~31 -> ~56; DL wall = CPU** |

## Ranked queue
1. **Confirm the fps pass on hardware** (`./run.sh hwdisc`, no burn scheduled by the maintainer): SD log
   `gfx:` + `cpu:` lines per state, PROF dumps. Hypotheses: hardware DL ~10-15 ms flight, 30 fps locked or
   better; menus no longer 13-19 fps; menu loads much shorter. Watch for anything frame-count based that
   now runs fast above 30 fps (Flycast showed none in flight).
2. **Visual bugs** (maintainer, hardware): column under the Rocket Belt pilot (reproduces in Flycast: gate
   ~132 s, build/evidence/cylinder-repro-flycast.png) and a big triangle/square under the hang glider
   (likely the same shadow draw, cimg==zimg / decal path); striped landing-target texture; pilots missing
   on pilot select; title pilot overlay sized wrong (compare with scripts/n64_oracle.sh frames).
3. Remaining menu load stalls: re-measure after 023 (Flycast: much shorter), then profile the app thread.
4. Audio fidelity (M5): reverb via AICA DSP, sub-frame parameter timing, PCM8 for waves that sound bad.
5. M5 parity (all vehicles/levels), M6 (fps on hardware), M7 release.
