#!/usr/bin/env bash
# emu_smoke.sh — the milestone gate (maintainer decision 2026-10-10: no CD-R burns).
# Boots an image in Flycast (built from source, docker/emu/Dockerfile) under Xvfb with the HLE BIOS
# and composite cable (480i), captures the serial console, waits for the milestone marker, then
# takes screenshots so the on-screen result is evidence too.
#   emu_smoke.sh [cdi] [marker] [timeout_s]
# Outputs: build/emu-serial.log, build/emu-shot-1.png (at the marker), build/emu-shot-2.png (+5 s).
# EMU_RECORD=40: record the first 40 s of the display to build/emu-run.mp4 (640x480, H.264).
# EMU_SHOTS="12 20 30": also capture build/emu-t<sec>.png at those seconds after launch (keeps running
# until the last one even after the marker).
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cdi=${1:-build/wings64-emu.cdi}
marker=${2:-WINGS64_M$(sed -n 's/^#define WINGS64_MILESTONE \([0-9]*\).*/\1/p' "$ROOT/src/dc/milestone.h")_OK}
timeout=${3:-120}
cdi=${cdi#"$ROOT/"}
[ -f "$ROOT/$cdi" ] || { echo "emu_smoke: $cdi missing (./run.sh disc --emu)" >&2; exit 2; }
img=$("$ROOT/scripts/ctr.sh" emu-tag)
docker image inspect "$img" >/dev/null 2>&1 || { echo "emu_smoke: image $img missing (scripts/ctr.sh emu-build)" >&2; exit 2; }
rm -f "$ROOT"/build/emu-serial.log "$ROOT"/build/emu-shot-*.png "$ROOT"/build/emu-t*.png

docker run --rm -v "$ROOT:/work" -w /work -e HOME=/tmp -e EMU_SHOTS="${EMU_SHOTS:-}" -e EMU_RECORD="${EMU_RECORD:-}" --user "$(id -u):$(id -g)" "$img" bash -c '
    ulimit -c 0
    cdi=$1 marker=$2 timeout=$3
    log=build/emu-serial.log
    Xvfb :99 -screen 0 800x600x24 -nolisten tcp >/dev/null 2>&1 &
    export DISPLAY=:99
    for _ in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
    flycast -config config:Debug.SerialConsoleEnabled=yes -config config:UseReios=yes \
            -config config:Dynarec.Enabled=yes -config window:fullscreen=no "$cdi" > "$log" 2>&1 &
    pid=$!
    if [ -n "$EMU_RECORD" ]; then
        rm -f build/emu-run.mp4
        # bounded: a stalled X server or emulator must not keep the container alive past the run
        timeout -s INT $(( EMU_RECORD + 30 )) ffmpeg -loglevel error -f x11grab -framerate 30 -video_size 640x480 -i :99.0+80,60 -t "$EMU_RECORD" \
            -c:v libx264 -preset veryfast -crf 23 -pix_fmt yuv420p build/emu-run.mp4 &
        rec=$!
    fi
    rc=1
    start=$(date +%s)
    for _ in $(seq "$timeout"); do
        el=$(( $(date +%s) - start ))
        for s in $EMU_SHOTS; do
            [ "$el" -ge "$s" ] && [ ! -f build/emu-t$s.png ] && xwd -root -silent | convert xwd:- build/emu-t$s.png
        done
        last=0; for s in $EMU_SHOTS; do last=$s; done
        if grep -aq "$marker" "$log" && [ "$el" -ge "$last" ]; then rc=0; break; fi
        if grep -aqE "WINGS64_M[0-9]+_FAIL|kernel panic|assertion failed|Unhandled exception|Verify Failed" "$log"; then break; fi
        kill -0 $pid 2>/dev/null || break
        sleep 1
    done
    sleep 3; xwd -root -silent | convert xwd:- build/emu-shot-1.png
    sleep 5; xwd -root -silent | convert xwd:- build/emu-shot-2.png
    [ -n "$EMU_RECORD" ] && wait $rec
    true
    kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null
    exit $rc
' bash "$cdi" "$marker" "$timeout"
rc=$?
grep -aE 'wings64|assetroot|romfs|WINGS64_|db:|panic|assert|xception|Verify' "$ROOT/build/emu-serial.log" | head -40
if [ $rc = 0 ]; then echo "emu_smoke: PASS ($marker)"; else echo "emu_smoke: FAIL (no $marker in ${timeout}s; see build/emu-serial.log)"; fi
exit $rc
