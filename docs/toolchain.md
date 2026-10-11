# Toolchain

## Pins (`docker/pins.env` is the source of truth; resolved 2026-10-10)
```
KOS_REV=c23410b3aa1ba6eeb00ac21a0634952edfaa1708   # tag v2.2.2 (lightweight)
TOOLCHAIN_PROFILE=14.3.0      # GCC 14.3.0, binutils 2.44, newlib 4.5.0.20241231
                              # at v2.2.2 the maker is utils/dc-chain (renamed kos-chain later on master);
                              # its fallback profile is 13.4.0 (no 13.2.0 profile); 15.1.0 = experiment
MKDCDISC_REV=2b98b0d9480e364e3517355648fbc3b49fb19882  # v0.0.4, main @ 2026-09-20 (no upstream tag)
DECOMP_REV=1a8558f5ceb320cce254d6da0a49a1f2bbc23e21
SH4ZAM: submodule external/sh4zam @ 7747e5d281a75dd4ffce10df2758748feb421c66
KOS_SH4_PRECISION=-m4-single-only   # KOS itself, newlib multilib and every port object
```
No ARM toolchain: KOS uses its prebuilt `stream.drv`. Add an `arm-eabi` stage only if custom
AICA firmware is ever built. Image tag = `wings64-dc:<sha256 of pins.env+Dockerfile+fetch-src.sh>`
(`scripts/ctr.sh tag`); colima uses the legacy builder (no buildx), so stages build sequentially.

## Container (primary; macOS arm64 host)
- Pattern: ssb64-dc `docker/Dockerfile` multi-stage (`src` → `toolchain-build` → `dev`),
  repo bind-mounted at `/work`, ROM never enters the image, `scripts/ctr.sh run <cmd>`.
- Host needs Docker (CLI present; start colima: `colima start --cpu 6 --memory 8`).
- First `./run.sh setup` compiles dc-chain (~1 h on 6 colima CPUs) unless the image tag already exists.

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

## Hardware (decided: CD-R only, composite video, SD card for logs)
- Burn `build/wings64.cdi` (padding on). No dcload cable exists; `run_hw.sh` just prints the
  path and a burn checklist.
- Logs: `DB_LOG_SD=1` mirrors `dbgio` to `/sd/wings64/log-<buildid>.txt` (link `-lkosfat`,
  mount at boot if present, never required). Flush per frame; cap at 8 MB.
- Perf numbers on screen (`DB_PERF` overlay) since there is no serial.
- Video: 480i NTSC on composite/S-video. 480p untested until a VGA box exists.
