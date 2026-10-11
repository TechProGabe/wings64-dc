# RESUME — handoff for the executing LLM

## Seed prompt (paste after a context flush)
> You are executing wings64-dc, a native Dreamcast port of Pilotwings 64 from
> its decomp. Read `CLAUDE.md`, `PLAN.md`, `docs/sh4zam-mandate.md`, then
> `kb/STATE.md` for the current milestone. Work milestone by milestone; every
> milestone ends with `./run.sh disc` producing a `.cdi` that boots. Use sh4zam
> for all math. Never commit ROM material. Mark departures `DIVERGES`. Report
> hardware results, not emulator ones, as evidence.

## Next-run seed prompt (after the frame-rate pass, written 2026-10-11)
> You are executing wings64-dc (~/Documents/GitHub/wings64-dc), a native Dreamcast port of Pilotwings 64
> from its decomp. Read `CLAUDE.md`, `kb/RESUME.md`, `kb/STATE.md` (current numbers + queue), `kb/traps.md`
> (the KOS scheduler and SD entries at the end), `docs/fps-plan.md` (results at the end). M0-M4 pass in the
> emulator gate; two hardware runs exist (audio good, SD log works, 15-25 fps before the fps pass). The fps
> pass (patches/023 + kill-switched changes) took Flycast to ~55-57 fps. If the maintainer brings hardware
> logs from a `./run.sh hwdisc` disc, read them first (`scripts/gfxstat.sh LOGNNN.TXT`, `cpu:` lines,
> `tools/prof/decode_prof.py PROFnnn.BIN <elf from the NAS>`). Otherwise work the queue: the visual bugs
> (the Rocket Belt column reproduces in Flycast), then menu loads, then M5. Binding rules: CLAUDE.md §1.
> The maintainer limits subagents (Fable consults; Haiku agents up to 5 when they clearly help).

## Where the port is
**M4 done 2026-10-11; frame-rate pass done in the emulator.** Music and SFX play in menus and flight
(`src/dc/audio`: the decomp's own players drive alSyn voices -> AICA channels; the whole Yamaha ADPCM pool
`disc/wings64.aud` sits in AICA RAM; docs/audio-design.md). Hardware runs (2026-10-11): boots from CD-R,
audio "sounds really good", SD card log works (backported KOS 1c6398f SD driver, src/dc/kos_sd), frame rate
was 15-25 fps, CPU-bound in the display-list interpreter. The fps pass (docs/fps-plan.md, kb/STATE.md
table) found the root cause: the decomp's kernel idle spin stole ~44% of every frame because KOS v2.2.2
ticks hand the CPU to any ready thread (patches/023 blocks it). Flycast: ~55-57 fps in every state, menus
reach flight in half the time. Hardware confirmation pending (no burn scheduled).
Visual bugs from hardware are queued (kb/STATE.md #2). Hardware discs: `./run.sh hwdisc` (FPS monitor, SD
log, profiler); copies go to the Jupiter NAS share under wings64-dc/ with a .src.json (commit, sha256).
To watch on the Mac: `./run.sh emu` (native Flycast) or `./run.sh play` (VNC).
Gate scripts use state-relative steps (`s<state>+ms`, src/dc/autoplay.c) re-armed after >0.5 s poll gaps;
run one emulator at a time; never turn the SD log on in Flycast. Never edit a script while a background
run is executing it (bash reads scripts lazily; the soak broke that way once).

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
./run.sh hwdisc           # hardware-test CD-R images (wings64.cdi + -dd): FPS monitor, SD log, SD profiler (L+R+Start)
./run.sh release          # build/wings64-release.cdi (no db.o, NDEBUG)
./run.sh test [--ci]      # host tests + tree checks (--ci: no ROM needed)
./run.sh run-hw           # no cable: prints the .cdi path to burn + checklist
./run.sh gate             # THE GATE: builds with the milestone's DB_AUTOPLAY script, runs smoke, records video
./run.sh soak             # M4 audio soak: 600 s attract mode, no input, WINGS64_M4_SOAK_OK
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
