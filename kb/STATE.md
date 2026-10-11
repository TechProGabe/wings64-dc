# STATE — current numbers and the ranked queue

Updated 2026-10-11 (M4 done: emulator gate + 10-min soak PASS; next: hardware test of the M4 disc).

| metric | value | evidence |
|---|---|---|
| milestone | **M4 done** (emulator: music + SFX in menus and flight, gate + 10-min soak); hardware test next | state-log 2026-10-11 M4 done |
| flight fps (emulator) | ~30 in the air, ~20 on the launch pad; DL ~30 ms/frame (was ~50 before the combiner cache) | `game:`/`gfx:` log lines, DB_GFX_PROF |
| settings menu | title "Settings": FPS monitor, SD card log; flags at EEPROM 0x7F8 on the VMU (persistence unverified in Flycast) | patches/021, src/dc/settings.c |
| hardware logging | SD card log + receipt + crash/assert hook (`src/dc/db.c`), OpenCrossing-proven SD path; untested on hardware | docs/testing.md "SD card log" |
| M0 disc | `build/wings64.cdi` build `574fd2b-1011-0141` (705 MB raw, padded, 80-min CD-R) sha256 `fd7fed8e46ec7887…` | `./run.sh disc` |
| toolchain | image `wings64-dc:ce773e5503b9`: GCC 14.3.0, KOS v2.2.2 `c23410b` (-m4-single-only), mkdcdisc v0.0.4 `2b98b0d` | `docker/pins.env` |
| decomp pin | `1a8558f`, **21 patches** (3 bitfield, 10 UB, 1 yield, 4 endian, arena, sched, settings menu; all `WINGS64_DC`-guarded) | `patches/README.md` |
| sh4zam pin | fork `7747e5d` (v0.9.1), built from source in `src/game/Makefile` | submodule |
| ROM fs slice | **5,582,256 B (5.32 MiB)**, crc32 `DAFCC147`, 1272 index entries = 1272 UV files, every FORM header verified | `tools/rom/slice_fs.py` |
| UV file mix | UVTX 463, UVMD 363, UVAN 115, UVBT 102, UVCT 101, UPWT 61, PDAT 25, 3VUE 12, UVFT 9, SPTH 8, UPWL 4, 1 each UVEN/UVLT/UVLV/UVSQ/UVSX/UVSY/UVTP/UVTR/ADAT | slice_fs |
| ELF size (M1, whole decomp linked) | text 826,808 · data 32,124 · bss 942,676 | `kos-size` |
| RAM at boot check (M1) | image 1,823 KB · heap 6,576 KB (ROM slice 5.3 MB + 1 MB text texture) · **free 7,984 KB** | emu screen |
| decomp build | 197 decomp TUs + 5 port TUs on decomp flags compile with 0 warnings; 41 libultra calls are `TODO` stubs (src/dc/ultra/stubs_todo.c) | M1 |
| host build | ILP32 armhf under qemu-arm: decomp + port + sh4zam SW link; unit tests + host M0 check pass | `scripts/test_host.sh` |
| purity | 202 decomp-flag TUs: no fsrra/fsca/fmac in GCC asm | `make purity` |
| emulator gate | **PASS** `WINGS64_M0_OK`, screen all green (Flycast v2.7 container, HLE BIOS, composite) | build/emu-shot-2.png |
| boot load (emulator) | 5.32 MiB in 2881 ms = 1892 KB/s (not hardware; CD-R expected 500 KB/s–1 MB/s) | serial log |
| fps on hardware | flight ~23, menus ~18-19, title ~20, results ~38 (target: locked 30); CPU-bound in the DL interpreter (PVR wait 7 us) | hw log 2026-10-11 |
| DL interpret on hardware (median) | flight 27 ms (max 75), file menu 43, class select 41, title 30, congratulations 69 | `gfx: wait/dl/finish` |
| gate (`./run.sh gate`) | M3 script, state-relative steps: title -> ... -> flight 108 s -> results 130 s, `WINGS64_M3_OK` + `WINGS64_M4_OK` (timeout 200 s; title waits ~0.9 s per music change since real-time audio) | build/emu-run.mp4 (untracked) |
| gfx (emulator) | menus ~60 fps; flight per frame (DB_GFX_PROF): conv ~7 ms, emit ~4, vtx ~3.5, state ~5 | `gfx:` log lines |
| menu load stalls (emulator) | Start game -> file menu ~7 s, file menu -> class select ~5 s of no frames (N64: ~1 s) | `game: t` log lines |
| known render gaps | none blocking: island terrain, sea, sky + clouds, buildings, pilots render; title matches the N64 reference within capture scale | M3 contact sheets (untracked) |
| audio | **AICA driver** (src/dc/audio): libaudio players unchanged, alSyn voices -> AICA channels 0-31, whole pool resident; reverb off (DIVERGES) | docs/audio-design.md |
| audio waves | music 57 (audio_tbl 0.80 MB VADPCM), SFX 107 (UVSX .TBL 1.43 MB, inside wings64.fs); all ADPCM, no RAW16 | `tools/audio/measure_pool.py` |
| AICA pool (disc/wings64.aud) | **1,860,032 B resident** (sfx 1,193,632 + music 666,400) of 1,900,544 B sample RAM; loops truncated at loop end, 5 waves decimated to 11025 Hz; upload ~2 s at boot (emulator) | `tools/audio/transcode.py` |
| ADPCM quality | VADPCM decode bit-exact (loop-state oracle); Yamaha SNR median 24.5 dB, worst 10.3 dB (saw-like music 0x52768); hardware listening decides pcm_force.txt | `test_audio.py --rom` |
| audio timing (emulator) | 300 audio frames / 5 s, 22.0-22.3 kHz virtual DAC, 0 drops in the gate; alAudioFrame 27-80 us avg | `aud: t` lines |

## M0 exit criteria (met in emulator 2026-10-10)
Screen shows, all green: `crc32 DAFCC147 manifest DAFCC147 match`, `UV files 1272 (index 1272, manifest 1272)`,
`FORM headers ok 1272/1272 walk end 618B6C`, `M0 RESULT: PASS`, and the `alive N s` counter keeps ticking.

## Ranked queue
1. **Frame rate on hardware** (maintainer, first hardware run: 15-20 fps, "needs to be improved").
   Done: SD log says CPU-bound in the DL interpreter (27 ms flight, 41-43 ms menus; PVR wait 7 us), about
   27 us per triangle. Next: hardware PC profile (DB_PROF disc, L+R+Start in flight and in a menu) ->
   tools/prof/decode_prof.py, then cut the top functions. Flycast per-phase timers distort (2x DL).
2. Hardware visual bugs: cylinder under the Rocket Belt pilot and a big triangle/square under the hang
   glider (maintainer; likely the shadow draw: cimg==zimg / decal path). The column REPRODUCES in Flycast
   (gate shot ~132 s, camera looking up while the belt climbs; build/evidence/cylinder-repro-flycast.png),
   striped landing-target texture, pilots missing on pilot select, title pilots overlay sized wrong
   (maintainer, 2026-10-11; compare with scripts/n64_oracle.sh frames).
3. Menu load stalls: 5-7 s with no controller polls after each menu change (N64 ~1 s; maintainer: "super
   laggy" on hardware too); profile the app thread.
4. (done on hardware 2026-10-11) SD log: KOS v2.2.2's SD driver never brought the card up (stuck "probing").
   src/dc/kos_sd backports sd.c + scif-spi.c from KOS 1c6398f (OpenCrossing's, proven on this card);
   8.3 names; on-screen "SD <status>" under the FPS counter. Flycast: fails cleanly ("SD FAIL INIT").
5. Audio fidelity (M5): reverb via AICA DSP (AL_FX_CUSTOM is off), sub-frame parameter timing, PCM8
   for waves that sound bad on hardware.
6. (done) M0-M4.
