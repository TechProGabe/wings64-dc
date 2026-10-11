# sh4zam integration brief (KallistiOS / Dreamcast)

Source clones: scratchpad/ref/sh4zam (fork, master @ 7747e5d, v0.9.1) and
~/sh4zam-upstream/sh4zam (fork branch docs-site, HEAD 74a86bf — handwritten docs under doc/site/).

## Authoritative docs, in order (headers win)
1. include/sh4zam/*.h (source of truth)
2. doc/site/src/content/docs/for-agents.md (agent rules, names that do NOT exist)
3. doc/site/src/content/docs/concepts/{xmtrx,sh4-fpu,types-and-layout,conventions,backends}.md
4. doc/site/src/content/docs/guides/{using-in-a-project,matrix-transforms,optimization,install-kallistios,interop}.md, cheatsheet.md
5. doc/tips.dox (compiler flags, FPU modes), doc/guide.dox
6. Built outputs (after `cd doc/site && npm ci && npm run build`): dist/llms.txt, dist/llms-full.txt, dist/llms-api.txt, public/api/index.json (every symbol: signature, header:line, backends).

## Build & consume
- Mostly header-only (include/sh4zam/inline/, backends inline/sh4/*_sh4.inl.h and inline/sw/*_sw.inl.h), BUT out-of-line code exists:
  source/shz_{matrix,quat,version,xmtrx}.c, source/sh4/shz_xmtrx_sh4.s, shz_mem_sh4.s, shz_complex_sh4.c. Link libsh4zam.a (or compile those sources in).
  ssb64-dc chose header-only: everything it calls is SHZ_INLINE; it only vendors include/ (+ source/ for the host SW-fallback test).
- KOS Makefile wraps kos-cmake; needs KOS_BASE. `make lib`, `make install` (symlinks into $KOS_PORTS). Also an official kos-ports package (unpinned HEAD — do not use; pin a rev).
- CMake: C11 (C17 rec.), C++20, static target `sh4zam`. SHZ_BACKEND=SH4|SPU|SW (SH4 default when PLATFORM_DREAMCAST, which kos-cmake sets). SHZ_TLS_MODEL default IMPLICIT. SHZ_LIBRARY_TYPE STATIC|OBJECT.
- Submodule: `git submodule add https://github.com/TechProGabe/sh4zam.git external/sh4zam`; plain KOS Makefile: `-Iexternal/sh4zam/include` + libsh4zam.a (or compile source/*.c + source/sh4/*.s directly).
- FLAGS: REQUIRED `-m4-single-only` (or -m4-single): SH4 backend only selected if __SH4_SINGLE__/__SH4_SINGLE_ONLY__ defined (shz_cdefs.h:63), else silent slow SW fallback.
  Recommended: `-O3 -ffast-math -ffp-contract=fast -mfsrra -mfsca -fomit-frame-pointer -flto -fipa-pta -fno-PIC -DNDEBUG`.
  Without -ffast-math FSCA/FSRRA still work via inline asm.
  bruces_balls example: `-std=gnu2x -O3 -fomit-frame-pointer -flto -fbuiltin -ffast-math -ffp-contract=fast -mfsrra -mfsca -fmerge-all-constants -funroll-loops -fno-PIC -fipa-pta -ftree-vectorize`, links `-lsh4zam -lkosutils -lm -Wl,--gc-sections`.
- Naming: C `shz_<module>_<verb>[_qual]`, types `shz_X_t`, macros `SHZ_`. C++ `<sh4zam/shz_sh4zam.hpp>` namespace shz (shz::vec3, shz::mat4x4, shz::quat, shz::xmtrx static). Umbrella `<sh4zam/shz_sh4zam.h>` or per-module shz_{scalar,trig,vector,quat,matrix,xmtrx,complex,mem,cdefs,version}.h.
- Macros: SHZ_INLINE, SHZ_FORCE_INLINE, SHZ_NO_INLINE, SHZ_LIKELY/UNLIKELY, SHZ_ALIGNAS(N), SHZ_MEMORY_BARRIER_SOFT/HARD(), SHZ_CONVERT(type,v), SHZ_PREFETCH(p).

## Modules / identifiers
- scalar: shz_sqrtf(_fsrra), shz_inv_sqrtf(_fsrra), shz_invf(_fsrra), shz_divf(_fsrra), shz_lerpf, shz_clampf, shz_fmaf, shz_floorf/ceilf/roundf, shz_dot6f, shz_dot8f, shz_mag_sqr3f/4f, shz_powf/expf/logf, shz_equalf. SHZ_F_PI, SHZ_F_PI_2, SHZ_F_TAU, SHZ_DEG_TO_RAD(), SHZ_FLT_EPSILON(0.01f).
- trig (FSCA): shz_sinf/cosf/tanf(+_deg), shz_sincosf, shz_sincosf_deg, shz_sincosu16 -> shz_sincos_t{sin,cos}; shz_atan2f, shz_asinf, shz_acosf.
- vector N=2,3,4: shz_vecN_{init,add,sub,scale,dot,cross(2/3),magnitude,magnitude_sqr,magnitude_inv,normalize,normalize_safe,lerp,reflect,swizzle,dot2,dot3}; shz_vec3_vec4(v,w); shz_vec3_deref(ptr); shz_vecN_from/to; _Generic shz_vec_dot etc. dot=FIPR, normalize=FIPR+FSRRA.
- matrix (memory): shz_mat4x4_t SHZ_ALIGNAS(8) column-major, .col[4]/.elem[16]/.left/.up/.forward/.pos. shz_mat4x4_{init_identity,init_perspective,init_lookat,mult(_unaligned/_transpose),transform_vec4,transform_point3,inverse,transpose,copy,decompose,to_quat}, shz_mat3x3_*. MOST shz_mat4x4_* CLOBBER XMTRX.
- xmtrx (register-resident XF0..XF15):
  load: shz_xmtrx_load_4x4, load_unaligned_4x4, load_wxyz_4x4, load_transpose_4x4, load_3x4, load_3x3
  store: shz_xmtrx_store_4x4, store_unaligned_4x4, store_3x4
  init: init_identity(_safe), init_translation, init_scale, init_rotation_{x,y,z,xyz,quat}, init_perspective, init_ortho, init_frustum, init_lookat, init_screen, init_permutation_wxyz
  apply (in place, cheapest): apply_{4x4,reverse_4x4,3x4,translation,scale,rotation_x/y/z/xyz/quat,perspective,screen,lookat,self}
  GL-style full multiply: translate, scale, rotate_x/y/z, translate_reverse
  fused: load_apply_4x4, load_apply_store_4x4, apply_store_4x4
  transform: transform_vec4, transform_vec3, transform_vec2, transform_point3, transform_point2
  misc: set_translation, get_translation, invert, determinant, to_quat, read/write(_row/_col)
- quat (W,X,Y,Z): shz_quat_{init,identity,from_axis_angle,from_angles_xyz,mult,normalize(_safe),slerp,nlerp,transform_vec3,conjugate,inv}.
- mem: shz_memcpy, shz_memmove, shz_memcpy{1,2,4,8,32,64,128}, shz_memcpy2_8, shz_memcpy2_16, shz_memcpy4_16, shz_memset8, shz_memset2_16, shz_memcpy32_1, shz_memswap32_1(_xmtrx); store queues shz_sq_memcpy32(_xmtrx), shz_sq_memcpy32_1(_xmtrx); shz_dcache_alloc_line(p) (whole 32B line yours), SHZ_PREFETCH(p).
  Alignment: memcpy32/64/128 dst 32B, src 8B. sq_*: src 8B, dst 4B.
- complex/fft, SPU backend: irrelevant for DC.

## Rules
- Names that DO NOT exist: shz_memset, shz_memset2, shz_memset4, shz_memset32, shz_macw, shz_xmtrx_trans_vec4 (-> transform_vec4), shz_xmtrx_load_4x4_wxyz (-> load_wxyz_4x4), shz_xmtrx_angles/position/size, shz_xmtrx_load_apply, inverse FFT.
- XMTRX is global (thread-local) register state; shz_xmtrx_* take no matrix arg. *_xmtrx mem routines and most shz_mat4x4_* clobber it. Reload after; batch work in registers; store once.
- KOS dc/matrix.h (mat_load/mat_apply/mat_transform/mat_perspective) uses the same bank. Pick ONE: sh4zam. KOS mat_perspective applies screen mapping implicitly; sh4zam needs explicit shz_xmtrx_apply_screen.
- FPU single-precision: never let a double into a TU that calls sh4zam; `f` literals always; float*uint32_t promotes to double (uint16_t does not). No FPSCR assert safety net exists.
- shz_mat4x4_t needs 8B alignment unless _unaligned; alignas(32) for dcache_alloc_line.
- _fsrra variants need positive input; shz_invf_fsrra(x) = 1/|x| (sign lost); _safe for degenerate input.
- Radians, column-major, right-handed world, left-handed clip.
- Prefer shz_* over libm everywhere (no double promotion, no call).

## Vertex transform pattern (example/bruces_balls/bruces_balls.c:97-241, example/pvr_dma/pvr_dma.c:211,939-945)
```c
#include <sh4zam/shz_sh4zam.h>
alignas(32) shz_mat4x4_t proj;                    // once per frame/init
shz_xmtrx_init_permutation_wxyz();                // FTRV yields W first
shz_xmtrx_apply_screen(640.0f, 480.0f);
shz_xmtrx_apply_perspective(SHZ_DEG_TO_RAD(60.0f), 640.0f/480.0f, 0.0f);
shz_xmtrx_store_4x4(&proj);
/* per object */
shz_xmtrx_load_4x4(&proj);
shz_xmtrx_translate(pos.x, pos.y, -pos.z);
shz_xmtrx_apply_rotation_x(rot.x); shz_xmtrx_apply_rotation_y(rot.y);
for (i = 0; i < n; i++) {                         // per vertex: one FTRV
    shz_vec4_t p = shz_xmtrx_transform_vec4(shz_vec3_vec4(v[i], 1.0f));
    p = shz_vec4_swizzle(p, 1, 2, 3, 0);          // undo WXYZ
    pvr_vertex_t* o = pvr_dr_target(dr); SHZ_MEMORY_BARRIER_SOFT();
    p.w = shz_invf_fsrra(p.w);                    // W>0 assumed
    o->x = p.x * p.w; o->y = p.y * p.w; o->z = p.w; pvr_dr_commit(o);
}
```
Also: shz_sincosf for geometry gen, SHZ_PREFETCH + shz_sq_memcpy32_1(pvr_dr_target(dr), hdr) for header submission, shz_dcache_alloc_line on 32B-aligned structs.
