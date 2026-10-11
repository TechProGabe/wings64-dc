# Testing

| layer | what | when |
|---|---|---|
| tree checks (`./run.sh test --ci`, no ROM) | `rom_leak_check` (no ROM bytes/known SHA in repo), `shz_names_check` (no non-existent sh4zam names; validates against `external/sh4zam/doc/site/dist/api/index.json` or a grep of headers), `diverges_check` (every `DIVERGES` has a reason), `patches_check` (each patch has a `.md`), `vramfence_check`, `no_double_check` (no `double` in sh4zam TUs) | pre-commit |
| purity (`fpu_check.py`) | disassemble objects; forbid FSRRA/FSCA/fmac in physics-pinned TUs; forbid KOS `mat_*` symbols anywhere | pre-commit |
| host tests (`src/dc/test`, ILP32 armhf under qemu-arm in the container) | decomp parsers on the fs slice vs expected struct values (swap correctness), MIO0 vs reference, Fast3D interpreter on captured DLs → golden vertex output, mesg-queue semantics, VMU save round trip; sh4zam `SHZ_BACKEND=SW` (algebra only, not accuracy) | `./run.sh test` (needs ROM) |
| audio codec (`tools/audio/test_audio.py`) | ROM-free (`--ci`): Yamaha ADPCM round-trip SNR on sine/chirp, VADPCM decode, decimation filter; `--rom`: VADPCM decode reproduces every `ALADPCMloop.state` bit-exactly, Yamaha SNR guards (worst >= 10 dB, median >= 22 dB) | `./run.sh test` |
| audio gate + soak (emulator) | the gate cannot hear, so `WINGS64_M4_OK` reads counters at the flight -> results point: music key-ons in menus and flight, >= 20 SFX key-ons, AICA KEYONB seen set in both, 0 wave misses, 0 audio-thread stall windows (< 270 frames per 5 s), virtual-DAC drops <= 2%; `./run.sh soak` (`DB_SOAK`): 600 s of title/attract mode with no input, `WINGS64_M4_SOAK_OK` when key-ons keep coming every minute and the same counters hold. `aud: t` lines every 5 s carry the numbers | M4 exit; any audio change |
| replay oracle | `DB_FDR_HASH`: hash of flight-recorder state per frame while playing the ROM's demo replays; must match between builds and between host and target for physics-adjacent changes | per milestone |
| emulator gate (`./run.sh smoke`) | containerized Flycast v2.7 (`docker/emu/`, Xvfb, HLE BIOS, composite) boots the `--emu` cdi; serial log must show `WINGS64_M<n>_OK`; `build/emu-shot-{1,2}.png` captured at the marker and +5 s | every milestone exit (maintainer decision 2026-10-10) |
| hardware | burned CD-R on the retail console: boot, milestone exit criteria, `DB_PERF` numbers via on-screen overlay and `/sd/WINGS64/LOG*.TXT` (SD adapter; no dcload); soak (`DB_SOAK`) 30 min for audio dropouts | per milestone |
| hardware profile (`DB_PROF`, on in `./run.sh hwdisc`) | PC sampling at the 1 kHz scheduler tick into 16-byte bins over .text (src/dc/prof.c); hold **L + R + Start** (or wait 240 s) and `WINGS64/PROFnnn.BIN` lands on the SD card; `tools/prof/decode_prof.py PROFnnn.BIN <elf>` prints the flat profile (use the ELF saved beside the image) | hardware perf work |
| frame budget (any build) | `gfx: ... dl avg30 wall W cpu C` (C < W means another thread stole the window), `cpu: tN P%` every 5 s per N64 thread (0 render, 1 kernel, 3 audio, 4 scheduler/interpreter, 6 app) from KOS thd_get_cpu_time; `scripts/gfxstat.sh [log]` prints fps and DL median/max per game state; DB_PROF also logs `prof: in-dl` (what runs while a display list is in progress) | perf work, emulator or hardware |
| hardware evidence format | `kb/state-log.md` entry: date, disc build id, console, video (composite/S-video), SD log file name, what was observed, numbers | always |

## SD card log (hardware)

The SD log is OpenCrossing-Dreamcast's hardware-proven SD path (`dc/src/dc_profdump.c`) applied
to the text log. Code: `src/dc/db.c`; knobs: `src/dc/db.h`.

**Card.** Use a serial-port SD adapter (jj1odm / DreamShell style, SCIF). Format the card
**MBR-partitioned FAT32 with 4 KB clusters**.
- A "superfloppy" card with no partition table does not enumerate.
- 32 KB clusters make fs_fat ask for about 260 KB of cache mid-run.

**Image.** Any dev build carries the logger, and the title-screen settings menu switches it on (Settings > SD card log; the choice is kept on the VMU, see `src/dc/settings.c`).
- `EXTRA_CFLAGS=-DDB_LOG_SD=1 ./run.sh disc` turns it on at boot.
- `-DDB_LOG_SD_IF=1` is only for a reader you know is SCI. With no card, the SCI probe waits forever.

**What happens when it is on.**
1. Text is buffered in RAM from boot.
2. About 6 s after the game thread starts (`DB_LOG_SD_ARM_MS`), the console prints `db: SD log arming` and goes silent.
3. The card owns the serial port from then on, so the adapter and a serial cable are mutually exclusive.
4. Each boot writes the next free `/sd/WINGS64/LOGNNN.TXT` and rewrites `/sd/WINGS64/RECEIPT.TXT` (8.3 upper-case names).
5. Every 2 s (`DB_LOG_SD_SYNC_MS`) the log is written and `fs_fat_sync`ed, so a power-off loses at most 2 s.
6. Switching it off in the menu unmounts the card and gives the console back.

**Reading the receipt.**
- `state running` with an old uptime means power-off or a hang at that time.
- A `crash`, `fatal` or `assert` state means the hook wrote its report.
- A non-zero `errors` count means writes failed.

**Crashes.** `DB_CRASH` is on in dev builds. A CPU exception (illegal instruction, address error) or a failed `assert` logs `WINGS64_CRASH` / `WINGS64_ASSERT` with the faulting thread, PC, PR, registers and 8 stack words. It then flushes the card and parks the thread. Symbolise with:

    sh-elf-addr2line -f -e build/wings64.elf <pc> <pr>

The ELF must be the one the disc was built from (build id in the log header and receipt).

**Emulator.** Flycast has no SD adapter, so an SD-on image is judged on hardware only. The crash hook is checked in Flycast with `GATE_CFLAGS=-DDB_CRASH_TEST=1 ./run.sh gate`; the gate is expected to fail there, with `WINGS64_CRASH illegal instruction` in the serial log. Flycast does not raise address errors for unaligned stores.
