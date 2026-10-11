# RESUME — handoff for the executing LLM

## Seed prompt (paste after a context flush)
> You are executing wings64-dc, a native Dreamcast port of Pilotwings 64 from
> its decomp. Read `CLAUDE.md`, `PLAN.md`, `docs/sh4zam-mandate.md`, then
> `kb/STATE.md` for the current milestone. Work milestone by milestone; every
> milestone ends with `./run.sh disc` producing a `.cdi` that boots. Use sh4zam
> for all math. Never commit ROM material. Mark departures `DIVERGES`. Report
> hardware results, not emulator ones, as evidence.

## Where the port is
M2 done 2026-10-11 (emulator gate PASS): the game boots on KOS threads, renders title/file menu/class
select through the Fast3D->PVR interpreter, and takes controller input. **Paused here by the maintainer.**
**M3 in progress (2026-10-11).** Flow already reaches flight and results with scripted input
(class select -> r -> A x4: pilot select 3, test overview 12, test details 2, test setup 4, test update 5,
results 6 ~24 s later with no stick). Fixed: vertex fog colour (KOS stub asserted -> reboot).
OPEN: in flight everything below the horizon is black (terrain, vehicle) and pilot select is black;
title-screen terrain renders. Not the BG plane depth (zclip 0). Next step: run the DB_GFX_TRACE build
(trace prints per-frame tris op/pt/tr | rej cull clip vtx w-range) with the script in kb/STATE.md and
read a state-5 frame; suspects: trivial reject/clip planes with large outdoor w, depth, culling sign.
Still to do for M3: bump `src/dc/milestone.h` to 3, gate script to results, 30 fps (emulator).
To watch on the Mac: `./run.sh play` (VNC; docs/flycast-macos.md). Milestones are gated by
`./run.sh smoke` (containerized Flycast; maintainer burns no discs). Host macOS Flycast is broken
(`kb/traps.md`); always use the container.

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
- No dcload cable: **CD-R only**. SD card adapter present → logs via `DB_LOG_SD` to `/sd/wings64/`.
- **Composite/S-video only** (480i). No VGA box yet.
- Console revision unknown (VA0/1/2) → build both default and `-L data-data` release images.
- CD-R brand/speed that boots reliably: ask when the first disc is burned; record here.
