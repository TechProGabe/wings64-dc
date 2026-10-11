# STATE — current numbers and the ranked queue

Updated 2026-10-11 (M3 done: emulator gate PASS; paused; M4 audio next).

| metric | value | evidence |
|---|---|---|
| milestone | **M3 done** (emulator gate PASS: Rocket Belt Beginner to results); M4 audio next | state-log 2026-10-11 M3 done |
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
| fps on hardware | — (target: locked 30) | — |
| gate (`./run.sh gate`) | M3 script, state-relative steps: title -> ... -> flight 89 s -> results 110 s, `WINGS64_M3_OK` | build/emu-run.mp4 (untracked) |
| gfx (emulator) | menus ~60 fps; flight per frame (DB_GFX_PROF): conv ~7 ms, emit ~4, vtx ~3.5, state ~5 | `gfx:` log lines |
| menu load stalls (emulator) | Start game -> file menu ~7 s, file menu -> class select ~5 s of no frames (N64: ~1 s) | `game: t` log lines |
| known render gaps | none blocking: island terrain, sea, sky + clouds, buildings, pilots render; title matches the N64 reference within capture scale | M3 contact sheets (untracked) |
| audio | **silent**: no AICA driver; PI reads of audio_tbl return zeros (DIVERGES until M4) | os_shim.c |
| audio waves | music 57 (audio_tbl 0.80 MB VADPCM), SFX 107 (UVSX .TBL 1.43 MB, inside wings64.fs); all ADPCM, no RAW16 | `tools/audio/measure_pool.py` |
| AICA pool (Yamaha ADPCM) | **1.92 MiB = music 0.71 + SFX 1.21; DOES NOT FIT** resident under 2 MiB − 256 KiB (over by 171 KiB) | measure_pool |
| waves > 65534 samples | 6 (music 110,432 / 104,224; SFX 77,312 / 81,968 / 110,432 / 85,344) → need streaming or splitting | measure_pool |

## M0 exit criteria (met in emulator 2026-10-10)
Screen shows, all green: `crc32 DAFCC147 manifest DAFCC147 match`, `UV files 1272 (index 1272, manifest 1272)`,
`FORM headers ok 1272/1272 walk end 618B6C`, `M0 RESULT: PASS`, and the `alive N s` counter keeps ticking.

## Ranked queue
1. **M4 audio** (PLAN.md M4): AICA driver; the pool does not fit resident (1.92 MiB vs 1.75 MiB budget):
   SFX resident (1.21 MiB) + page music; stream or split the 6 waves over 65534 samples.
2. Frame rate to a 30 fps floor (M6, or earlier if cheap): launch-pad view ~30 ms DL in the emulator.
   Biggest phases: vertex conversion to PVR (~7 ms), emit (~4), vertex transform (~3.5). Measure on
   hardware before deep work (Flycast timing is not hardware).
3. Menu load stalls: 5-7 s with no controller polls after each menu change (N64 ~1 s); profile the app thread.
4. Hardware run when the maintainer burns one: SD log + receipt, FPS monitor numbers (docs/testing.md).
5. (done) M0-M3.
