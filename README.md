# wings64-dc

Native Sega Dreamcast port of **Pilotwings 64** (Nintendo 64, 1996), built from the
[Pilotwings64Decomp](https://github.com/gcsmith/Pilotwings64Decomp) matching
decompilation on [KallistiOS](https://github.com/KallistiOS/KallistiOS), with
[sh4zam](https://github.com/TechProGabe/sh4zam) as the SH-4 math layer.

**Status: M0 (toolchain + boot check disc).** `PLAN.md` holds the architecture and milestones;
`CLAUDE.md` holds the rules; `kb/RESUME.md` is where an executing agent starts.

## Build
Requires Docker (colima on macOS) and your own US ROM copied to `baserom.z64` at the repo root.
```
./run.sh setup     # container (GCC 14.3.0 + KOS v2.2.2 + mkdcdisc, ~25 min first time), submodules, decomp
./run.sh disc      # build/wings64.cdi, padded for an 80-min CD-R
./run.sh test      # tree checks + ROM slice validation
```

No game content is included or will ever be committed. Building requires your own
legally obtained US ROM (`docs/rom-provenance.md`).

## Acknowledgements (planned lineage)
- Pilotwings64Decomp (gcsmith et al., MIT) — the game.
- Birdman64 (NoahVl, MIT) — the patch set and libultra surface for this exact game.
- ssb64-dc (beigeceleron, MIT) — repo structure, disc pipeline, testing discipline.
- sm64-dc / sf64-dc / mk64-dc (jnmartin84; gfx core Emill/MaikelChan BSD; sf64-dc CC0) — Fast3D→PVR interpreter and AICA voice driver lineage.
- DCA3 (skmp) — texture/VRAM and streaming lessons.
- sh4zam (Falco Girgis) — XMTRX, FIPR, FSCA, FSRRA.
