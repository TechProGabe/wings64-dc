# RESUME — handoff for the executing LLM

## Seed prompt (paste after a context flush)
> You are executing wings64-dc, a native Dreamcast port of Pilotwings 64 from
> its decomp. Read `CLAUDE.md`, `PLAN.md`, `docs/sh4zam-mandate.md`, then
> `kb/STATE.md` for the current milestone. Work milestone by milestone; every
> milestone ends with `./run.sh disc` producing a `.cdi` that boots. Use sh4zam
> for all math. Never commit ROM material. Mark departures `DIVERGES`. Report
> hardware results, not emulator ones, as evidence.

## Next-run seed prompt (M4 audio, written 2026-10-11)
> You are executing wings64-dc (~/Documents/GitHub/wings64-dc), a native Dreamcast port of
> Pilotwings 64 from its decomp. Read `CLAUDE.md`, `kb/RESUME.md`, `PLAN.md` (D6, M4, decisions 7-9),
> `kb/STATE.md`, `kb/traps.md`. M0-M3 are done in the emulator gate; start **M4 audio**: music + SFX in
> menus and flight, no dropouts over 10 min (emulator gate; hardware numbers are hypotheses).
>
> **Step 1, before any audio code: spawn a Fable agent (Agent tool, model "fable") as a design
> consultant** and give it these facts to rule on: the ADPCM pool is 1.92 MiB (music 0.71 + SFX 1.21,
> `tools/audio/measure_pool.py`) against ~1.75 MiB of AICA RAM after driver/ring buffers, so it does not
> fit resident; 6 waves exceed 65534 samples (music 110,432 / 104,224; SFX 77,312 / 81,968 / 110,432 /
> 85,344); music waves live in audio_tbl (ROM 0x6314D0+, not in the RAM image: PI reads there return
> zeros today, `src/dc/ultra/os_shim.c`), SFX in UVSX .TBL inside wings64.fs; 16 MB main RAM is tight;
> the ARM7 is too weak for mixing. Ask it to choose and justify: resident vs paged music (per level or
> per sequence), streaming vs splitting the long waves, PCM fallback rules for >+1 octave pitch, where
> audio_tbl lives (RAM image extension vs disc reads at level load), the alSyn voice -> AICA channel
> mapping and command queue (sm64-dc `aica_synth.c` lineage, `snd_sh4_to_aica`), audio-thread timing
> against vblank, and a test plan that works in Flycast (the gate cannot hear: log voice starts, wave
> ids, AICA channel state; record audio in the emulator container if possible). Get its answer as a
> short design note, write it to `docs/audio-design.md`, then execute it.
>
> Rules that bind (CLAUDE.md): never commit ROM material, extracted waves, ADPCM pools or recordings;
> sh4zam for float math, no double, -m4-single-only; never edit the decomp in place (patches/ + .md,
> WINGS64_DC guards, `DIVERGES:` comments); every optimization has a kill switch; agents don't run git,
> the main thread commits; releases are local only. Use up to 10 Haiku agents for parallel pieces
> (wave extraction tool, transcoder, host tests) and Opus agents for large self-contained features.
> Gate with `./run.sh gate` (state-relative autoplay steps; one emulator at a time; never turn the SD
> log on in Flycast). End M4 with `./run.sh disc`, kb/STATE.md + kb/state-log.md updates, and a commit.

## Where the port is
**M3 done 2026-10-11 (emulator gate PASS).** Title -> file menu -> class select -> pilot select -> test
overview/details/setup -> Rocket Belt Beginner flight -> results, driven by `./run.sh gate`
(`WINGS64_M3_OK`). Flight renders the island terrain, sea, sky with clouds, buildings, pilots and HUD;
the title matches the N64 reference photo. Frame rate (emulator): ~30 fps in the air, ~20 fps on the
launch pad (display list ~30 ms/frame); the 30 fps floor everywhere is carried to M6 (kb/STATE.md queue).
Port features: title-screen **Settings** menu (FPS monitor, SD card log; patches/021, `src/dc/settings.c`),
SD card log + crash/assert hook for hardware runs (`src/dc/db.c`, docs/testing.md "SD card log").
**Audio is silent**: no AICA driver yet, sample reads return zeros. **Next: M4 audio** (PLAN.md).
Paused here by the maintainer.
To watch on the Mac: `./run.sh emu` (native Flycast, docs/flycast-macos.md) or `./run.sh play` (VNC).
Milestones are gated by `./run.sh gate` (containerized Flycast; the maintainer burns no discs).
Gate scripts use state-relative steps (`s<state>+ms`, src/dc/autoplay.c): menu loads pause controller
polling for seconds. Run one emulator at a time (two starve the Docker VM). Never turn the SD log on in
Flycast (the probe stalls the main monitor thread).

## Reference clones used for planning (re-clone when needed; shallow is fine)
| what | where | pin |
|---|---|---|
| decomp | https://github.com/gcsmith/Pilotwings64Decomp | `1a8558f` |
| sh4zam (user fork) | https://github.com/TechProGabe/sh4zam | master `7747e5d` (v0.9.1); docs on branch `docs-site` (`74a86bf`) |
| ssb64-dc | https://github.com/beigeceleron/ssb64-dc | HEAD at 2026-10-10 |
| Birdman64 | https://github.com/NoahVl/Birdman64 | v1.0 (2026-10-03) |
| sm64-dc | https://github.com/jnmartin84/sm64-dc branch `ootpvr` | `6be7a00` |
| sf64-dc | https://github.com/jnmartin84/sf64-dc | `4ed5c8d` |
| mk64-dc | https://github.com/jnmartin84/mk64-dc | `1cbad2e` |
| DCA3 mirror | https://github.com/emtee40/dca3-game | `02d0103` |
| KallistiOS | https://github.com/KallistiOS/KallistiOS | tag v2.2.2 |
| mkdcdisc | https://gitlab.com/simulant/mkdcdisc | ≥ v0.0.4 (2026-09-20) |

The user's local sh4zam checkout with built docs: `~/sh4zam-upstream/sh4zam`
(branch `docs-site`, `doc/site/dist/llms*.txt` and `dist/api/index.json` present).

## Build lines (exist since M0; names are the contract)
```
./run.sh setup            # container image at docker/pins.env, submodules, hooks
./run.sh build            # build/wings64.elf
./run.sh disc             # build/wings64.cdi      (padding on → CD-R)
./run.sh disc --emu       # build/wings64-emu.cdi  (mkdcdisc -N)
./run.sh release          # build/wings64-release.cdi (no db.o, NDEBUG)
./run.sh test [--ci]      # host tests + tree checks (--ci: no ROM needed)
./run.sh run-hw           # no cable: prints the .cdi path to burn + checklist
./run.sh gate             # THE GATE: builds with the milestone's DB_AUTOPLAY script, runs smoke, records video
                          #   GATE_CFLAGS=-DDB_GFX_PROF=1 / -DDB_GFX_TRACE=1 / -DDB_CRASH_TEST=1 add debug knobs
./run.sh play             # Flycast in the container over VNC -> macOS Screen Sharing (password wings64)
./run.sh smoke            # emulator smoke: containerized Flycast, waits for WINGS64_M<n>_OK, screenshots build/emu-shot-*.png
./run.sh shell            # shell inside the pinned container
./run.sh decomp-refresh   # re-fetch decomp at pin and re-apply patches/
./run.sh emu              # Flycast GUI on build/wings64-emu.cdi with serial console
```

## Measurement rules
0. Milestone exits are judged in the emulator gate (maintainer, 2026-10-10). Emulator numbers are labelled "emulator".
1. A number without a hardware run is a hypothesis. 2. Every perf change ships
with a kill switch and a before/after pair (us/frame from `DB_PERF`, screenshot).
3. Flycast cannot falsify a cache-locality claim. 4. Record numbers in
`kb/STATE.md`, evidence in `kb/state-log.md` (newest first). 5. Replays
(`DB_FDR_HASH`) must hash identically before and after any physics-adjacent change.

## What is known to be hard (see PLAN.md §3)
H1 endianness · H2 Fast3D→PVR fidelity · H3 alSyn→AICA · H4 scheduler semantics ·
H5 IDO UB + fixed memory map · H6 float determinism · H7 ROM-in-RAM fit/load time.

## Facts that live only in a human's head (answered 2026-10-10)
- No dcload cable: **CD-R only**. SD card adapter present → logs to `/sd/wings64/log-NNN.txt` (Settings
  menu or `DB_LOG_SD=1`; MBR FAT32, 4 KB clusters; docs/testing.md).
- **Composite/S-video only** (480i). No VGA box yet.
- Console revision unknown (VA0/1/2) → build both default and `-L data-data` release images.
- CD-R brand/speed that boots reliably: ask when the first disc is burned; record here.
