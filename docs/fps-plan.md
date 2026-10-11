# Frame-rate plan (post hardware run 1)

Design consult (Fable, 2026-10-11) after the first two hardware runs; executed from here. Hardware numbers
(kb/state-log.md 2026-10-11): PVR idle (wait 7 us, finish 90 us); the SH-4 display-list interpreter costs
25-27 ms per flight frame and 41-45 ms in menus, the app thread ~15 ms more, serially. Target: DL + app
<= 33 ms (the scheduler dequeues a gfx task only on a retrace, so that locks 30 fps).

## 1. Where the time most likely goes (static evidence)
- No double / soft-float on the per-triangle path (`sizeof(double) == 4` under -m4-single-only; no
  `__adddf3` etc. in gfx objects). No timer reads per triangle unless DB_GFX_PROF.
- Flycast: ~12 ms flight DL = ~2,500 instruction-cycles per emitted triangle; hardware doubles it.
1. **I-cache conflict misses (hardware only).** 8 KB direct-mapped I-cache (index = addr & 0x1FFF).
   gfx_f3d_run (5.2 KB, run_dl/do_vtx inlined), do_tri (2.6 KB), draw_state (3 KB), to_pvr, gpvr_tri_pv,
   pvr_list_prim, memcpy alias each other: hot code ~10 KB, tens of line refills per triangle.
2. **Per-triangle copies + write-allocate misses**: gpvr_tri_pv copies 3x32 B to the stack, pvr_list_prim
   memcpys again into the RAM vertex buffer; every 32 B output line is a write-allocate fill.
3. **Per-draw-state texture work**: sample_hash (16 strided loads over game RAM), lookup, cc_prepare
   copies, gpvr_set_state memcmp + pvr_poly_compile, >= 136 times a frame.
4. **Branch-heavy triangle path**: do_tri trivial reject = 15 fcmp/bt, guard-band test 15 more, cull det;
   to_pvr 13 ftrc + 8 clamp branches; do_vtx reloads XMTRX on every G_VTX (mvp_dirty never tested).
5. **Rect path (menus)**: rect_colors runs cc_compile + generic combine per texrect/fillrect instead of the
   fitted sFit; each glyph pays a full draw_state + header memcmp. Why menus cost 41-45 ms.
6. **Texture upload spikes** (75 ms frames): two divides per texel in the pow2 resample, per-texel format
   switch and morton() in the twiddle.
7. **App thread ~15 ms**: game logic + DL build in 197 decomp TUs at -Os; serial with the interpreter.

## 2. Ranked changes (kill switch, default on once it passes)
| # | change | expected on hw | check without hw | switch |
|---|---|---|---|---|
| 1 | Direct vertex emit into the list's vertex buffer (dcache line alloc, no stack copy, no pvr_list_prim/memcpy per tri) | -2..-4 ms | Flycast emit ~0, DL -0.8..-1.5 ms, screenshots identical | `WINGS64_DIRECT_EMIT` |
| 2 | I-cache layout: cold paths out (clipper, trace, rects, uploads, mtx) with cold/noinline; hot path in one `.text.hot.gfx` section < 8 KB, disjoint from hot KOS callees | -3..-6 ms | static nm table only | `WINGS64_HOT_LAYOUT` |
| 3 | Texture memo per frame + lazy hash | -1.5..-3 ms flight, -3..-6 menus | tex phase, hit counts equal | `WINGS64_TEX_MEMO` |
| 4 | Rect colours from the fit; skip redundant set_state | menus -3..-6 ms | file-menu state phase, screenshots | `WINGS64_RECT_FIT` |
| 5 | Outcodes in do_vtx, cheap reject/guard test, lazy XMTRX reload, per-epoch hoists | -1..-1.5 ms | fcmp count, tris equal | `WINGS64_OUTCODES`, `WINGS64_MVP_LAZY` |
| 6 | Texture upload fast path (row/x maps, per-format loops, morton tables) + host test | spikes 75 -> ~30 ms | max DL per gate | `WINGS64_FAST_TEXCONV` |
| 7 | -ffast-math -ffp-contract=fast for gfx_f3d/gfx_pvr only | -0.3..-0.8 ms | screenshots within 1 LSB | `WINGS64_GFX_FASTMATH` |

## 3. App side: -Os -> -O2
Likely 2-4 ms of the ~15 ms. No IEEE single result changes (-ffp-contract=off stays); `make purity` and a
replay diff guard determinism. Mechanism: `OPT_O2_SRC` list, knob `WINGS64_DECOMP_O2` (0 all -Os, 1 list,
2 all). Rank TUs with a Flycast PC profile (instruction-weighted; fine for ranking, not cycles).

## 4. Execution order
0. Tooling: serial dump of the PC profile in Flycast, `app` time per frame, `scripts/gfxstat.sh` (median DL
   per game state), hot-function nm table as the layout oracle.
1. Change 1. 2. Change 5. 3. Change 2 (payoff hardware-only; say so). 4. Changes 3 + 4. 5. Change 6.
6. App -O2. 7. Change 7 last.
Expected before the next hardware run: Flycast flight DL ~12 -> 7-8 ms, menus ~25 -> 12-14; hardware flight
DL 25-27 -> ~13-16 ms if the I-cache reading holds (30 fps lock), ~18-20 if not (then -O2 decides).
