# CLAUDE.md — wings64-dc

**wings64-dc** — native Sega Dreamcast port of Pilotwings 64 (N64), built from
the `gcsmith/Pilotwings64Decomp` matching decomp on KallistiOS with **sh4zam**.
Target: **retail Dreamcast, stock 16 MB**, SH-4 @ 200 MHz, PowerVR CLX2, AICA.
Output: a ready-to-burn `.cdi`. Status: **M3 done (emulator PASS); paused; M4 audio next** — an
executing LLM starts at `kb/RESUME.md`.

This file is an index plus the rules that must never be violated. It carries no
status narrative: state lives in `kb/STATE.md`, the handoff in `kb/RESUME.md`.

## 1. Hard rules

- **Never commit ROM material or disc images.** No `.z64/.n64/.v64`, no
  `baserom*`, no `.cdi/.iso/.gdi`, no `wings64.fs`, no ADPCM pool, no extracted
  asset. `tools/check/rom_leak_check.py` runs in `./run.sh test --ci`. The ROM is
  `baserom.z64` at the repo root (gitignored), SHA-1 in `docs/rom-provenance.md`;
  never write its path into a tracked file.
- **sh4zam is mandatory** for every float/matrix/trig/memcpy path
  (`docs/sh4zam-mandate.md`). No KOS `mat_*`. No `double`. `-m4-single-only`.
  Use only names that exist in the pinned sh4zam headers; the name lint fails
  the build otherwise.
- **Never edit the decomp in place.** It is fetched to `external/pw64-decomp/`
  (gitignored) and patched from `patches/` (one patch per reason, `#ifdef WINGS64_DC`
  guard where possible). Compat goes in shadow headers under `src/dc/decomp/`.
  Every behavioural departure in port code is marked `DIVERGES: <reason>`.
- **No upstream PRs to the decomp** (its CONTRIBUTING bans LLM contributions).
  We consume it under MIT at a pinned revision.
- **Stock 16 MB.** No 32 MB dependency. **The MMU stays off.**
- **Every optimization has a kill switch** (compile-time `WINGS64_*` or `DB_*`
  knob) and the default is the good build.
- **Emulator is the milestone gate** (maintainer decision 2026-10-10: no CD-R burns).
  `./run.sh gate` builds the scripted-input image and `./run.sh smoke` boots the `--emu` image in pinned Flycast (container, HLE BIOS,
  composite 480i) and must print the milestone marker; its screenshots are evidence.
  Flycast is not cycle-accurate: cache, timing and fps claims stay **hypotheses**
  labelled "emulator" in `kb/STATE.md` until a console run exists.
- **Physics TUs use exact sh4zam variants** (no `_fsrra`/FSCA); purity check
  enforces it so `fdr.c` replays stay deterministic.
- **Releases are cut locally** with `./run.sh release`, never by CI.
- **Branch:** `main` only. Agents must not run git; the main thread commits.

### Hardware contract
8 MB VRAM (textures twiddled 16-bit / VQ, never linear RGBA8), no shaders, no
hardware T&L, one texture unit; vertex transform + lighting run on SH-4 via
XMTRX. 2 MB AICA RAM, 64 ADPCM channels, weak ARM7 (no CPU work there).
VMU ≈ 100 KB user data (PW64 save is 0x208 bytes). CD-R ≈ 500 KB/s–1 MB/s.

## 2. Start here

| order | file | why |
|---|---|---|
| 0 | `kb/RESUME.md` | fresh context: seed prompt, build lines, where the port is |
| 1 | `PLAN.md` | architecture decisions D1–D10, milestones M0–M7, hard problems, risks |
| 2 | `docs/sh4zam-mandate.md` | binding math rules + libultra→sh4zam table + RSP loop sketch |
| 3 | `kb/STATE.md` | current numbers and the ranked queue |
| 4 | `kb/closed.md` | rejected approaches — do not re-propose |
| 5 | `kb/traps.md` | gotchas already paid for by the reference ports |

## 3. Document map

| file | contents |
|---|---|
| `docs/architecture.md` | the decided layering and data flow, directory layout to create |
| `docs/toolchain.md` | pins, container, native fallback, commands |
| `docs/disc-pipeline.md` | ROM slice → disc/ → mkdcdisc → .cdi; emulator vs CD-R variants |
| `docs/testing.md` | host tests, oracles, tree checks, hardware procedure |
| `docs/sh4zam-brief.md` | sh4zam quick reference (build, modules, identifiers) |
| `docs/rom-provenance.md` | ROM identity and where it lives |
| `docs/reference/decomp-portability.md` | what the decomp is, file:line facts, UV formats, verdict |
| `docs/reference/birdman64-sm64dc.md` | this game's patch set + libultra surface (Birdman64); Fast3D→PVR interpreter + AICA driver (sm64-dc) |
| `docs/reference/ssb64-dc.md` | repo/build/disc/testing structure to copy |
| `docs/reference/jnmartin-ports-dca3.md` | sf64-dc/mk64-dc/DCA3 lessons |
| `docs/reference/prior-art-web.md` | toolchain state 2026, mkdcdisc flags, Flycast CLI |
| `kb/levers.md` | optimizations and memory levers still live |
