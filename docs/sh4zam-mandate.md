# The sh4zam mandate

Every float operation in the port goes through sh4zam unless this chapter says
otherwise. Use only identifiers that exist in `include/sh4zam/*.h` of the pinned
sh4zam revision. If unsure, grep the header or `doc/site/dist/api/index.json`
(fork branch `docs-site`, after `npm run build`): if a name is not there, it does
not exist. Companion quick-reference: `docs/sh4zam-brief.md`.

Verification notes from the research pass (keep in mind while executing):
- N64 `float mf[4][4]` (row vectors, translation in `mf[3][0..2]`) has the SAME
  memory layout as sh4zam column-major (`col[3]` = translation). No transpose
  load is needed. Only multiply ORDER reverses: `guMtxCatF(a,b,r)` = load b,
  apply a. `shz_xmtrx_apply_4x4(M)` computes XMTRX = XMTRX·M
  (`inline/sw/shz_xmtrx_sw.inl.h:72-82, 589`).
- Lighting must NOT use `shz_xmtrx_transform_vec3` (would clobber the MVP).
  Use in-memory, non-clobbering `shz_mat4x4_transform_vec3_transpose` once per
  light per matrix change.
- The KOS `mat_*` names in N.5 come from KOS `dc/matrix.h`/`matrix3d.h`; verify
  against `$KOS_BASE/include/dc/matrix.h` at the pinned KOS rev.
- `include/sh4zam/shz_xmtrx.h:9-16` has a stale `\todo` list; `init_rotation_quat`,
  `apply_store_4x4`, `invert` DO exist (lines 395, 658, 854).
- tips.dox claims FPU-mode asserts; they do not exist (only alignment/size
  asserts in debug builds). optimization.md is correct.
- The upstream docs never give an official CFLAGS line; N.1 rule 10 is the
  Bruce's Balls example's, plus `-m4-single-only` from KOS `environ.sh`.
- Unverified until tested on host with `SHZ_BACKEND=SW`: `guRotateRPYF` angle
  order mapping, and the Fast3D `G_MTX_*` flag encoding Pilotwings uses.

## N.1 Rules the executing LLM must follow

1. **FPU mode.** Build with `-m4-single-only`. Backend auto-detection
   (`shz_cdefs.h:62-68`) selects `SHZ_BACKEND == SHZ_SH4` only when the target
   is SH4 **and** `__SH4_SINGLE__` or `__SH4_SINGLE_ONLY__` is defined; otherwise
   it silently falls back to `SHZ_SW`. A `-m4` or `-m4-double-only` build must
   pass `SHZ_BACKEND=SW`.
2. **No doubles.** Every literal `1.0f`. Decomp code is full of IDO-era double
   literals and `(f64)` casts; convert them all in the platform layer and via
   the prelude (`#define f64 float` is NOT acceptable where the decomp relies on
   double range; audit each). A `double` in a function that inlines sh4zam asm
   can leave the FPU in double mode. No assert catches this. Avoid `float`
   arithmetic with `u32` (slow double path); `s32` and `u16` are fine.
3. **Radians.** All sh4zam angles are radians unless the name ends `_deg`;
   `u16` variants take 65536 = one turn. libultra `gu*` angles are degrees:
   wrap in `SHZ_DEG_TO_RAD()`.
4. **Column-major.** `shz_mat4x4_t.col[3]` (`.pos`) is the translation.
   Quaternions are W-first (`shz_quat_init(w,x,y,z)`).
5. **Alignment.** `shz_mat4x4_t` must be 8-byte aligned. N64 `float mf[4][4]`
   is usually 4-byte aligned: use `shz_xmtrx_load_unaligned_4x4`,
   `shz_xmtrx_store_unaligned_4x4`, `shz_xmtrx_apply_unaligned_4x4`,
   `shz_mat4x4_copy_unaligned` on it, or realign the type (preferred: make
   `Mtx`/`Mtx4F` 8-aligned in the prelude).
6. **XMTRX discipline.** `shz_xmtrx_*` calls operate on the FPU back bank. These
   CLOBBER it: most `shz_mat4x4_*` (every `init_*`, `apply*`, `mult*`,
   `transpose`, `inverse`, `copy_unaligned`, `swap`); every `*_xmtrx` memory
   routine (`shz_sq_memcpy32_xmtrx`, `shz_sq_memcpy32_1_xmtrx`,
   `shz_memswap32_1_xmtrx`); `shz_xmtrx_apply_store_4x4`. These do NOT clobber:
   `shz_mat4x4_transform_vec*/point*` (incl. `_transpose`), `shz_mat4x4_copy`,
   `shz_mat4x4_3x3_inverse*`, `shz_mat4x4_inverse_block_triangular`.
7. **`_fsrra` and `_safe`.** `shz_invf_fsrra(x)` returns 1/|x|; use only when
   w > 0 is guaranteed, else `shz_invf`. `shz_vec3_normalize_safe` wherever a
   zero vector is possible. `SHZ_FLT_EPSILON` = 0.01f.
8. **Names that do NOT exist:** `shz_memset`, `shz_memset2`, `shz_memset4`,
   `shz_memset32`, `shz_macw` (only `shz_memset8`, `shz_memset2_16` exist);
   `shz_xmtrx_trans_vec4` (use `shz_xmtrx_transform_vec4`);
   `shz_xmtrx_load_4x4_wxyz` (real: `shz_xmtrx_load_wxyz_4x4`);
   `shz_xmtrx_angles/position/size`; any inverse FFT; any fixed-point matrix.
9. **Link and include.** `#include <sh4zam/shz_sh4zam.h>`; link `-lsh4zam`
   (or compile `source/*.c` + `source/sh4/*.s` in). Release: `-DNDEBUG`.
10. **CFLAGS** (example's, plus `-m4-single-only`):
    ```
    -m4-single-only -std=gnu2x -O3 -fomit-frame-pointer -flto -fbuiltin -ffast-math -ffp-contract=fast -mfsrra -mfsca -fmerge-all-constants -funroll-loops -fno-PIC -fipa-pta -ftree-vectorize
    ```
    Link: `-lsh4zam -lkosutils -lm -Wl,--gc-sections`. If `-ffast-math` breaks a
    TU, drop it for that file only (sh4zam falls back to inline FSCA/FSRRA asm).

## N.2 libultra → sh4zam translation table

Layout: `guMtxF` memory == sh4zam column-major memory. Order reverses: N64
`A×B` → sh4zam load B, apply A. Build in XMTRX, store with
`shz_xmtrx_store_unaligned_4x4(&mf[0][0])` or `shz_xmtrx_store_4x4` if 8-aligned.

| libultra | sh4zam | Notes |
|---|---|---|
| `guMtxIdentF` | `shz_xmtrx_init_identity` | `shz_mat4x4_init_identity` also clobbers XMTRX |
| `guTranslateF` | `shz_xmtrx_init_translation(x,y,z)` | |
| `guScaleF` | `shz_xmtrx_init_scale(x,y,z)` | |
| `guRotateF(m,a,x,y,z)` | `shz_xmtrx_init_rotation(SHZ_DEG_TO_RAD(a),x,y,z)` | normalizes axis; `_dir` needs unit axis |
| `guRotateRPYF(m,r,p,h)` | `shz_xmtrx_init_rotation_xyz(...)` radians | VERIFY order on host; switch to `_zyx` if it differs |
| `guMtxCatF(a,b,r)` | `shz_xmtrx_load_apply_store_4x4(&r,&b,&a)` | `_unaligned` variant for `float[16]` |
| `guMtxXFMF` | `shz_xmtrx_transform_point3` (batch) / `shz_mat4x4_transform_point3` (one-off, no clobber) | w=1, no divide |
| `guMtxXFML` | convert Mtx→float first | |
| game-local invert | `shz_mat4x4_inverse` (clobbers) / `shz_mat4x4_inverse_block_triangular` (affine) / `shz_xmtrx_invert` | `shz_xmtrx_invert` is out-of-line, round-trips memory |
| `guNormalize(&x,&y,&z)` | `shz_vec3_normalize_safe(shz_vec3_init(x,y,z))` | |
| `guPerspectiveF(m,&pn,fovy,asp,n,f,s)` | `shz_xmtrx_init_perspective(SHZ_DEG_TO_RAD(fovy), asp, n)` | infinite far; drop `perspNorm`/`scale`; `shz_xmtrx_init_frustum` if real far plane needed |
| `guOrthoF` | `shz_xmtrx_init_ortho(l,r,b,t,n,f)` | w=1: derive PVR depth from z, not 1/w |
| `guFrustumF` | `shz_xmtrx_init_frustum` | |
| `guLookAtF` | `shz_xmtrx_init_lookat(eye, at, up)` | |
| `guLookAtReflectF` | `init_lookat` then `shz_xmtrx_read_row(0)`/`read_row(1)` for right/up | fills `LookAt` |
| `guMtxF2L` / `guMtxL2F` | none, plain C | prefer eliminating: `Mtx` → union holding 8-aligned float matrix so `gSPMatrix` carries floats; convert ROM `Mtx` at load (s15.16 → `(s32)` → `* (1.0f/65536.0f)`, with endian swap) |
| `sinf`/`cosf` | `shz_sinf`/`shz_cosf`; `shz_sincosf` for both (one FSCA) | |
| `sins`/`coss` (u16) | `shz_sincosu16` | same 65536-per-turn |
| `sqrtf` | `shz_sqrtf` (`shz_sqrtf_fsrra` when x>0); `1/sqrtf` → `shz_inv_sqrtf` | |
| `atan2f`, `fabsf` | `shz_atan2f`, `shz_fabsf`; also `shz_floorf`, `shz_fmodf`, `shz_powf` | |

## N.3 The per-frame RSP replacement loop

Game logic builds display lists and may use XMTRX freely in its `gu*` calls.
During display-list execution the RSP replacement OWNS XMTRX: it holds
`Pwxyz·Screen·P·MV` and nothing else may touch it. Each modelview/projection
change re-derives XMTRX with one fused load+apply.

```c
#include <sh4zam/shz_sh4zam.h>
static shz_mat4x4_t proj_raw, proj_scr, mv[10], tmp;  /* 8-aligned by type */
static int  top; static bool dirty = true;
static shz_vec3_t L_obj[3];           /* lights, object space */
static shz_vec4_t out[32];            /* sx, sy, 1/w, w */

void rsp_matrix(const shz_mat4x4_t *m, unsigned flags) {   /* gSPMatrix */
    if (flags & G_MTX_PROJECTION) {
        if (flags & G_MTX_LOAD) shz_mat4x4_copy(&proj_raw, m);
        else { shz_xmtrx_load_apply_store_4x4(&tmp, &proj_raw, m); shz_mat4x4_copy(&proj_raw, &tmp); }
        shz_xmtrx_init_permutation_wxyz();       /* W comes out of FTRV first */
        shz_xmtrx_apply_screen(640.0f, 480.0f);  /* NDC -> PVR pixels, y flipped */
        shz_xmtrx_apply_4x4(&proj_raw);
        shz_xmtrx_store_4x4(&proj_scr);
    } else {
        if ((flags & G_MTX_PUSH) && top < 9) { shz_mat4x4_copy(&mv[top + 1], &mv[top]); ++top; }
        if (flags & G_MTX_LOAD) shz_mat4x4_copy(&mv[top], m);
        else { shz_xmtrx_load_apply_store_4x4(&tmp, &mv[top], m); shz_mat4x4_copy(&mv[top], &tmp); }
    }
    dirty = true;   /* gSPPopMatrix: --top; dirty = true; */
}

void rsp_vertex(const Vtx *v, int n, int v0, const shz_vec3_t L_world[3]) {
    if (dirty) {    /* re-derive lights (no clobber), then XMTRX = proj_scr * mv */
        for (int l = 0; l < 3; ++l)
            L_obj[l] = shz_vec3_normalize_safe(shz_mat4x4_transform_vec3_transpose(&mv[top], L_world[l]));
        shz_xmtrx_load_apply_4x4(&proj_scr, &mv[top]);
        dirty = false;
    }
    for (int i = 0; i < n; ++i) {               /* n <= 32 */
        SHZ_PREFETCH(&v[i + 1]);
        shz_vec4_t p = shz_xmtrx_transform_vec4(shz_vec4_init(v[i].v.ob[0], v[i].v.ob[1], v[i].v.ob[2], 1.0f));
        p = shz_vec4_swizzle(p, 1, 2, 3, 0);     /* undo WXYZ */
        float iw = (p.w > 0.0f) ? shz_invf_fsrra(p.w) : 0.0f;  /* w<=0: near-clip it */
        out[v0 + i] = shz_vec4_init(p.x * iw, p.y * iw, iw, p.w);
        shz_vec3_t nrm = shz_vec3_scale(shz_vec3_init(v[i].n.n[0], v[i].n.n[1], v[i].n.n[2]), 1.0f / 127.0f);
        shz_vec3_t d   = shz_vec3_dot3(nrm, L_obj[0], L_obj[1], L_obj[2]); /* one FIPR batch */
        /* color = ambient + sum(color_l * shz_saturatef(d.e[l])) */
    }
}
```

- **Lighting.** Lights move into object space once per matrix change, so
  per-vertex lighting is FIPR only and XMTRX is never reloaded. For
  `gSPLookAt` texgen (`G_TEXTURE_GEN`, used by Pilotwings' env-map path),
  transform the lookat X/Y vectors the same way, then
  `shz_vec2_t st = shz_vec3_dot2(nrm, lookX_obj, lookY_obj);`.
- **Fog.** N64-style `shz_clampf(p.z * iw * fm + fo, 0.0f, 255.0f)` with
  `fm`/`fo` from `gSPFogPosition`, or drive PVR hardware vertex fog from `iw`
  as `example/pvr_dma` does. Pilotwings uses `gSPFogPosition(factor*1000,1000)`.
- **Viewport.** `shz_xmtrx_apply_screen(640,480)` assumes full-screen viewport.
  Pilotwings renders 320x240 with a 4x-fixed `Vp`. For a custom viewport apply
  `shz_xmtrx_translate(tx,ty,0.0f); shz_xmtrx_scale(sx,sy,1.0f);` (y negative
  for down) in place of `apply_screen`.
- **Near clipping.** Vertices with w ≤ 0 need near-Z clipping before
  submission. `example/pvr_dma/` has a tested implementation.
- **`G_MTX_*` flags.** Pilotwings uses Fast3D (NOT F3DEX2): no inverted push
  bit. Confirm against `include/PR/gbi.h` in the decomp.

## N.4 Memory

| Job | Routine | Rules |
|---|---|---|
| PVR vertex/header to the TA | `shz_sq_memcpy32_1(dst, src)` after `SHZ_PREFETCH(src)` | src 8-aligned, dst 4-aligned; store queues bypass cache: external memory only |
| Bulk to VRAM/TA | `shz_sq_memcpy32` | src 8-aligned, dst 4-aligned, bytes multiple of 32 |
| KOS DR path | `pvr_dr_target`/`pvr_dr_commit` + `SHZ_MEMORY_BARRIER_SOFT()` before reading results | Bruce's Balls pattern |
| DMA vertex buffer in RAM | `shz_dcache_alloc_line(p)` per 32-byte `pvr_vertex_t` before writing ALL 32 bytes | MOVCA.L zero-fills the line; partial ownership corrupts neighbours |
| Cart-DMA replacement (`osPiStartDma` → memcpy from preloaded bundle) | `shz_memcpy` default; `shz_memcpy32` when dst 32-aligned, src 8-aligned, size multiple of 32, `shz_memcpy` tail | never `sq_` into cached RAM; byte-swap big-endian data afterwards (sh4zam has no byteswap) |

Never call `*_xmtrx` memory variants while display lists are executing.

## N.5 KOS interop

- KOS `dc/matrix.h` / `matrix3d.h` (`mat_load`, `mat_store`, `mat_identity`,
  `mat_apply`, `mat_transform`, `mat_trans_single*`, `mat_perspective`,
  `mat_lookat`, `mat_rotate*`, `mat_translate`, `mat_scale`) use the SAME back
  bank. Treat each as an XMTRX clobber and DO NOT USE THEM in this port.
  `shz_xmtrx_apply_screen` replaces `mat_perspective`'s implicit screen mapping.
  KOS `pvr_*` (lists, DR, vertbuf DMA, textures) is unaffected and is used.
- Thread safety: XMTRX API is per-thread via TLS (`SHZ_TLS_IMPLICIT` on SH4).
  Other threads cannot corrupt your XMTRX, but anything in the SAME thread can.
  Run display-list execution on one thread with no foreign math between
  `rsp_matrix` and the last `rsp_vertex`. Docs do not cover interrupt or
  context-switch cost for the back bank; measure before adding threads.

## N.6 Which doc answers which question

Paths relative to the sh4zam repo root (fork `TechProGabe/sh4zam`, branch
`docs-site`). `doc/site/dist/` is build output: `cd doc/site && npm ci && npm run build`
(`npm run gen` alone writes `doc/site/public/api/*.json`).

| Question | Read |
|---|---|
| Agent rules, names that don't exist | `doc/site/src/content/docs/for-agents.md` |
| Every signature on one page | `doc/site/src/content/docs/cheatsheet.md` |
| Does symbol X exist; params, warnings, clobbers | `doc/site/dist/api/index.json` (schema `dist/api/schema.json`; per module `dist/api/xmtrx.json` etc.) |
| Site map with key facts | `doc/site/dist/llms.txt`; bundles `dist/llms-api.txt`, `dist/llms-full.txt` |
| Suffix meanings | `concepts/naming-and-suffixes.md` |
| Column-major, radians, epsilon | `concepts/conventions.md`, `concepts/types-and-layout.md` |
| XMTRX, FTRV timing, WXYZ trick | `concepts/xmtrx.md`, `guides/optimization.md` |
| Backend selection, TLS | `concepts/backends.md`, `include/sh4zam/shz_cdefs.h` |
| FIPR/FSCA/FSRRA, store queues, prefetch | `concepts/sh4-fpu.md` |
| Flags, double promotion, fast division | `guides/optimization.md`, `doc/tips.dox` |
| init/apply/set/store pattern | `guides/matrix-transforms.md` |
| Wrapping an existing math API | `guides/interop.md` |
| T&L + store-queue reference code | `guides/examples.md`, `example/bruces_balls/`, `example/pvr_dma/` |
| Linking, CMake | `guides/using-in-a-project.md`, `guides/install-cmake.md` |

(`concepts/` and `guides/` live under `doc/site/src/content/docs/`.)
