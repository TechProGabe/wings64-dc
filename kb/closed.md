# closed — settled questions. Do not re-propose without new evidence.

| idea | verdict | why |
|---|---|---|
| GLdc as the renderer | **no** | every N64→DC port that started on GLdc moved to raw PVR (sm64-dc 2024→2026, mk64-dc README). One texture unit + our own combiner evaluation need direct poly-header control |
| N64Recomp / static recompilation | **no** | needs an RT64-class renderer and a MIPS-recompiled binary; the decomp is 100% C — a platform-layer port is strictly cheaper |
| Full build-time display-list baking (ssb64-dc MeshBaker) | **not first** | PW64 assets are opaque blobs whose display lists are generated at load by the game's own parsers (`texture.c:506-518, 663-671`); baking happens at load time in M6 instead, behind a kill switch |
| Streaming assets from disc per level in v0.1 | **no** | ROM data region ≈5.5 MB fits RAM; ROM-in-RAM removes disc layout/prefetch engineering. Per-level loading stays a lever (`kb/levers.md`) |
| Offline byte-swapping the whole ROM slice | **no** | the game reads most fields byte-wise; only enumerated bulk copies need swapping, and Birdman64 already mapped them. Swapping in place after copy keeps the disc file byte-identical to the ROM for provenance checks |
| KOS `mat_*` / `matrix3d.h` anywhere | **no** | shares XMTRX with sh4zam; mandate forbids |
| Software mixer on SH-4 (alSyn/aspMain HLE into a KOS stream) | **no (fallback only)** | 32 voices at 22050 Hz costs CPU the renderer needs; AICA hardware voices are the proven path (sm64-dc, sf64-dc, ssb64-dc) |
| Custom ARM7 firmware (ssb64-dc) in v0.1 | **later** | KOS stock driver + `snd_sh4_to_aica` suffices for voice control; custom firmware only if command latency or sequencer offload is needed |
| SH4 MMU segment mapping (sf64-dc) | **no** | MMU off is a hard rule (store-queue QACR path); PW64's K0 addresses map onto SH-4 P1 with one OR, so no remap is needed |
| `-ffast-math` globally | **no** | arms FSRRA/FSCA inside physics/collision; per-TU only where purity allows |
| Submodule for the decomp | **no** | fetched + patched copy (`external/pw64-decomp/`, gitignored) so the repo carries no third-party source and patches are first-class; `./run.sh decomp-refresh` re-applies |
| kos-ports sh4zam package | **no** | unpinned GitHub HEAD; use the fork as a pinned submodule |
| Widescreen / 60 fps rate patches from Birdman64 | **no** | out of scope; 4:3, N64 timing |
| OpenCrossing-Dreamcast / tuxracer-dreamcast / Melee-X as structural models | **no** (maintainer directive 2026-10-10) | ssb64-dc + Birdman64 + jnmartin84 ports + DCA3 are the references |
