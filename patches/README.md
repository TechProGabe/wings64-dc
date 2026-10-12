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

## Own additions since (2026-10-11)
| patch | reason |
|---|---|
| 017 | memory map: RDRAM arena instead of fixed KSEG0 literals |
| 018 | scheduler RSP status write |
| 019 | UB: `uvMat4CopyF2L` debug fault store on matrix overflow dropped |
| 020 | UB: camera arrays declared shorter than used (ground view polygon); terrain culling |
| 021 | port feature: title-screen Settings menu, FPS overlay hook, overlay font helper (`DIVERGES`) |
| 022 | M4 audio: the audio task completes at once instead of queuing behind CPU display-list work (`DIVERGES`) |
| 023 | kernel thread's idle spin blocks instead (KOS ticks gave the spin ~44% of every frame) (`DIVERGES`) |
| 024 | the game's `_uvDebugPrintf` diagnostics forwarded to the port log, rate-limited (hardware hang triage) |
| 025 | port splash: "TECHPROGABE presents" before the title, game backdrop/font/SFX (`DIVERGES`, WINGS64_SPLASH) |
| 026 | screen fade-outs draw over the frozen last frame (PVR clears every frame) (`DIVERGES`, WINGS64_FB_FREEZE) |
| 027 | debug camera aimed at the landing pad (off unless WINGS64_DBG_LOOK_AT_PAD=<units>) |
| 028 | photo album keeps a persistent frame buffer (read back from VRAM) (`DIVERGES`, WINGS64_FB_PERSIST) |
