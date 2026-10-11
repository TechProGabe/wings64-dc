# patches/ — changes to the fetched decomp (external/pw64-decomp @ DECOMP_REV)

`scripts/fetch_decomp.sh` applies these in name order with `patch -p1`. Rules (CLAUDE.md):
- One patch per reason, `NNN-<reason>.patch` plus `NNN-<reason>.md` (why, origin). `patches_check` enforces it.
- Every change is wrapped in `#ifdef WINGS64_DC ... #else <original> #endif`, so the N64 build is byte-identical.
- Never edit `external/pw64-decomp/` by hand; change a patch and run `./run.sh decomp-refresh`.

## Import from Birdman64 (2026-10-10)
Birdman64 (MIT) ships 76 per-file patches against decomp 2529dab (one commit behind our pin). Their hunks
were sorted by reason; kept: bitfield layout, real decomp UB, cooperative yields, endianness. Dropped:
64-bit pointer width (SH-4 is ILP32 like the VR4300), frame-rate decoupling, widescreen/HUD anchors,
HLE framebuffer hooks, trace logging, and the present tick in `sched.c`. Their hand-edited hunks have
inaccurate `@@` counts, so they were applied by exact context match and re-diffed; every hunk applied.
Own additions: 002 (setimg opcode), 010 (debug vertex overrun), 011 (media copy fault).
