# levers — optimizations and memory levers still live (not yet needed)

| lever | what | when |
|---|---|---|
| L1 per-level disc loading | load only the UV files a level needs from `/cd/` using the filetable; frees ~3-4 MB RAM, cuts boot load | only if ROM-in-RAM budget breaks or boot load is unacceptable |
| L2 native geometry bake | UVCT/UVMD → PVR vertex runs + precompiled headers at load (PLAN D4 phase B) | M6, by profile |
| L3 VQ textures | offline/at-load VQ for the largest UVTX | if VRAM pressure appears |
| L4 store-queue header submission | `shz_sq_memcpy32_1` for poly headers | M6 |
| L5 custom ARM7 firmware | sequencer on ARM7 (ssb64-dc) | if SH-4 audio thread cost shows in `DB_PERF` |
| L6 AICA DSP reverb | AL_FX_CUSTOM → DSP program | M5 polish |
| L7 `-O3` hot list growth | per-TU by measurement | M6 |
| L8 strip Pipe/Tile/LoadSync | no-op already in interpreter; skip decode cost | M6 |
| L9 2 KB-aligned VRAM allocator | DCA3 `alloc.cpp` idea | if texture churn fragments VRAM |
| L10 I-cache layout | `.text.hot.gfx` < 8 KB, `-falign-functions=32` | M6 |
| L11 drop UVSX .TBL from the RAM image | SFX waves (1.43 MB VADPCM) live inside wings64.fs; once transcoded to AICA RAM they are dead weight in main RAM | M4, if RAM is tight |
| L12 split/stream long waves | 6 waves exceed 65534 samples; split at loop points or stream via a PCM ring (sm64-dc) | M4 (required) |
