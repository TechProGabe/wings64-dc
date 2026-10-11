#!/usr/bin/env bash
# emu_smoke.sh — the milestone gate (maintainer decision 2026-10-10: no CD-R burns).
# Boots an image in Flycast (built from source, docker/emu/Dockerfile) under Xvfb with the HLE BIOS
# and composite cable (480i), captures the serial console, waits for the milestone marker, then
# takes screenshots so the on-screen result is evidence too.
#   emu_smoke.sh [cdi] [marker] [timeout_s]
# Outputs: build/emu-serial.log, build/emu-shot-1.png (at the marker), build/emu-shot-2.png (+5 s).
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cdi=${1:-build/wings64-emu.cdi}
marker=${2:-WINGS64_M0_OK}
timeout=${3:-120}
cdi=${cdi#"$ROOT/"}
[ -f "$ROOT/$cdi" ] || { echo "emu_smoke: $cdi missing (./run.sh disc --emu)" >&2; exit 2; }
img=$("$ROOT/scripts/ctr.sh" emu-tag)
docker image inspect "$img" >/dev/null 2>&1 || { echo "emu_smoke: image $img missing (scripts/ctr.sh emu-build)" >&2; exit 2; }
rm -f "$ROOT"/build/emu-serial.log "$ROOT"/build/emu-shot-*.png

docker run --rm -v "$ROOT:/work" -w /work -e HOME=/tmp --user "$(id -u):$(id -g)" "$img" bash -c '
    cdi=$1 marker=$2 timeout=$3
    log=build/emu-serial.log
    Xvfb :99 -screen 0 800x600x24 -nolisten tcp >/dev/null 2>&1 &
    export DISPLAY=:99
    for _ in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
    flycast -config config:Debug.SerialConsoleEnabled=yes -config config:UseReios=yes \
            -config config:Dynarec.Enabled=yes -config window:fullscreen=no "$cdi" > "$log" 2>&1 &
    pid=$!
    rc=1
    for _ in $(seq "$timeout"); do
        if grep -aq "$marker" "$log"; then rc=0; break; fi
        if grep -aqE "WINGS64_M[0-9]+_FAIL|kernel panic|assertion failed|Unhandled exception|Verify Failed" "$log"; then break; fi
        kill -0 $pid 2>/dev/null || break
        sleep 1
    done
    sleep 3; xwd -root -silent | convert xwd:- build/emu-shot-1.png
    sleep 5; xwd -root -silent | convert xwd:- build/emu-shot-2.png
    kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null
    exit $rc
' bash "$cdi" "$marker" "$timeout"
rc=$?
grep -aE 'wings64|assetroot|romfs|WINGS64_|db:|panic|assert|xception|Verify' "$ROOT/build/emu-serial.log" | head -40
if [ $rc = 0 ]; then echo "emu_smoke: PASS ($marker)"; else echo "emu_smoke: FAIL (no $marker in ${timeout}s; see build/emu-serial.log)"; fi
exit $rc
