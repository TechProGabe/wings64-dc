#!/usr/bin/env bash
# n64_oracle.sh — reference frames from a real N64 emulator (native arm64 mupen64plus 2.5.9 built in
# docker/n64oracle, glide64mk2 by default, Xvfb, software GL) for visual parity checks. Uses the maintainer's
# baserom.z64 (bind-mounted, never in an image). Screenshots go to build/n64ref/ (untracked: they render ROM assets).
#   n64_oracle.sh <frame,frame,...> [gfx-plugin]      frames are VI retraces (60 per second)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
frames=${1:?usage: n64_oracle.sh frame,frame,...}
gfx=${2:-mupen64plus-video-glide64mk2}
[ -f "$ROOT/baserom.z64" ] || { echo "n64_oracle: baserom.z64 missing" >&2; exit 2; }
mkdir -p "$ROOT/build/n64ref"
docker run --rm -v "$ROOT:/work" -w /work -e HOME=/tmp --user "$(id -u):$(id -g)" \
    wings64-n64oracle:dev bash -c '
    ulimit -c 0
    Xvfb :98 -screen 0 640x480x24 -nolisten tcp >/dev/null 2>&1 &
    export DISPLAY=:98
    for _ in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
    timeout 1800 /usr/local/bin/mupen64plus --noosd --nosaveoptions --resolution 640x480 --windowed \
        --corelib /usr/local/lib/libmupen64plus.so.2.0.0 --plugindir /usr/local/lib/mupen64plus \
        --datadir /usr/local/share/mupen64plus --emumode 2 --gfx "$2" \
        --set "Core[ScreenshotPath]=/work/build/n64ref" --testshots "$1" ./baserom.z64 > build/n64ref/log.txt 2>&1
    ' bash "$frames" "$gfx"
ls "$ROOT/build/n64ref"
