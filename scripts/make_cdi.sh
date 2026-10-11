#!/usr/bin/env bash
# make_cdi.sh — master build/wings64*.cdi with mkdcdisc (PLAN D9). Runs in the container.
#   make_cdi.sh            build/wings64.cdi           CD-R: data-track padding ON
#   make_cdi.sh --emu      build/wings64-emu.cdi       -N, no padding (Flycast / ODE)
#   make_cdi.sh --data-data  adds -L data-data          late VA2 consoles that reject audio/data
#   RELEASE=1              build/wings64-release*.cdi  from build/wings64-release.elf
# disc/ is staged with -D (root excluded), so files appear at /cd/<name>.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/scripts/dc_env.sh"
cd "$ROOT"

emu=0; layout=""
for a in "$@"; do
    case "$a" in
        --emu) emu=1 ;;
        --data-data) layout=data-data ;;
        *) echo "make_cdi: unknown option $a" >&2; exit 2 ;;
    esac
done

base=wings64
[ "${RELEASE:-0}" = 1 ] && base=wings64-release
elf=build/$base.elf
[ -f "$elf" ] || { echo "make_cdi: $elf missing (./run.sh build first)" >&2; exit 1; }
out=build/$base
[ -n "$layout" ] && out=$out-dd
[ $emu = 1 ] && out=$out-emu
out=$out.cdi

# ROM slice: cut once from the maintainer's ROM (SHA-1 gated inside slice_fs.py).
if [ ! -f disc/wings64.fs ] || [ ! -f disc/fsmeta.txt ] || [ baserom.z64 -nt disc/wings64.fs ]; then
    python3 -I tools/rom/slice_fs.py
fi
cat build/build-id.txt > disc/disc.id

# Manifest: nothing but known outputs may ride on the disc.
allowed="disc.id wings64.fs fsmeta.txt"
for f in disc/*; do
    n=$(basename "$f")
    case " $allowed " in *" $n "*) ;; *) echo "make_cdi: unexpected file disc/$n (not in manifest)" >&2; exit 1 ;; esac
done

args=(-e "$elf" -D disc -n "Pilotwings 64" -a "wings64-dc" -V WINGS64 -r "$(date -u +%Y%m%d)"
      -o "$out" --allow-overwrite --no-banner)
[ $emu = 1 ] && args+=(-N)
[ -n "$layout" ] && args+=(-L "$layout")
mkdcdisc "${args[@]}"

size=$(stat -c %s "$out")
# CDI stores raw 2352-byte sectors; padding fills toward an 80-min (360000-sector) CD-R.
limit=$((360000 * 2352))
[ "$size" -lt "$limit" ] || { echo "make_cdi: $out is $size bytes (> 80-min CD-R raw $limit)" >&2; exit 1; }
echo "make_cdi: $out  ($((size / 1048576)) MB, build $(cat disc/disc.id))"
