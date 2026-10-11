# Toolchain

## Pins (`docker/pins.env`, to be created in M0)
```
TOOLCHAIN_PROFILE=14.3.0          # kos-chain profile; fallback 13.2.0 (legacy); experiment 15.2.0 (stable)
KOS_URL=https://github.com/KallistiOS/KallistiOS.git
KOS_REV=<commit of tag v2.2.2>    # resolve and pin the SHA, not the tag
KOS_PORTS: none required (sh4zam via submodule; no GLdc)
DECOMP_URL=https://github.com/gcsmith/Pilotwings64Decomp.git
DECOMP_REV=1a8558f5ceb320cce254d6da0a49a1f2bbc23e21
SH4ZAM: git submodule external/sh4zam → https://github.com/TechProGabe/sh4zam.git @ 7747e5d281a75dd4ffce10df2758748feb421c66
MKDCDISC_URL=https://gitlab.com/simulant/mkdcdisc.git
MKDCDISC_REV=<commit ≥ v0.0.4, 2026-09-20>
ARM: arm-eabi from kos-chain (only if custom AICA firmware is ever built; optional stage)
IMAGE_REGISTRY=ghcr.io/techprogabe/wings64-dc   # optional
```

## Container (primary; macOS arm64 host)
- Pattern: ssb64-dc `docker/Dockerfile` multi-stage (`src` → `toolchain-build` → `dev`),
  repo bind-mounted at `/work`, ROM never enters the image, `scripts/ctr.sh run <cmd>`.
- Host needs Docker (CLI present; start colima: `colima start --cpu 6 --memory 8`).
- First `./run.sh setup` compiles kos-chain (~1 h) unless a published image exists.

## Native fallback (optional)
KOS `utils/kos-chain/doc/macos.md` (Homebrew, Apple Silicon supported). Install to
`/opt/toolchains/dc`, source `environ.sh`, then `./run.sh --native build`.

## Flags (`scripts/dc_env.sh`)
```
KOS_SH4_PRECISION=-m4-single-only
COMMON: -fno-PIC -fno-PIE -fomit-frame-pointer -mfsrra -mfsca -ffunction-sections -fdata-sections
DECOMP TUs: -Os -funsigned-char -fwrapv -fno-strict-aliasing -ffp-contract=off -ftrivial-auto-var-init=zero
            -std=gnu11 -D_LANGUAGE_C -DVERSION_US -DBUILD_VERSION=VERSION_D -D_FINALROM -DNDEBUG -DTARGET_N64
            -DNON_MATCHING -DAVOID_UB -DRECOMP_BUILD -DWINGS64_DC
            -I src/dc/decomp -I external/pw64-decomp/src -idirafter external/pw64-decomp/include
            -Wno-implicit-int -Wno-implicit-function-declaration -Wno-return-type (decomp only)
PORT TUs:   -O2 (-O3 hot list) -std=gnu2x -ffast-math -ffp-contract=fast (gfx/audio only; NOT physics-adjacent)
PHYSICS-PINNED decomp TUs: additionally no -ffast-math; purity check forbids FSRRA/FSCA/fmac
LINK: kos-cc ... -lsh4zam -lm -Wl,--gc-sections -Wl,-Map=build/wings64.map   (no LTO before M6)
```
Per-TU `-O0` quarantine list (`OPT_QUARANTINE_SRC`) exists from day one and starts empty.

## Flycast (local smoke)
```
/Applications/Flycast.app/Contents/MacOS/Flycast -config config:Debug.SerialConsoleEnabled=yes build/wings64-emu.cdi
```
No headless mode; `.elf` loads directly with `-config bios.UseReios=yes`.

## Hardware
CD-R: burn `build/wings64.cdi` (padding on) with the usual tools (ImgBurn/DiscJuggler-compatible
burner on the user's side). dcload (`run_hw.sh`: `dc-tool-ser`/`dc-tool-ip -x build/wings64.elf -c disc`)
only if a cable exists — ask the maintainer.
