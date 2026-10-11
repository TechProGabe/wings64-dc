#!/usr/bin/env bash
# emu_play.sh — interactive Flycast for the Mac: the container runs Flycast under Xvfb and exports the
# display over VNC on localhost:5900 (password "wings64"); macOS Screen Sharing connects to it.
# Keyboard input reaches Flycast's default keyboard mapping. No audio (none exists before M4).
#   emu_play.sh [cdi]      stop with: docker rm -f wings64-play
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cdi=${1:-build/wings64-emu.cdi}
cdi=${cdi#"$ROOT/"}
[ -f "$ROOT/$cdi" ] || { echo "emu_play: $cdi missing (./run.sh disc --emu)" >&2; exit 2; }
img=$("$ROOT/scripts/ctr.sh" emu-tag)
docker rm -f wings64-play >/dev/null 2>&1 || true
docker run -d --name wings64-play -p 127.0.0.1:5900:5900 -v "$ROOT:/work" -w /work -e HOME=/tmp \
    --user "$(id -u):$(id -g)" "$img" bash -c '
    Xvfb :99 -screen 0 640x480x24 -nolisten tcp >/dev/null 2>&1 &
    export DISPLAY=:99
    for _ in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
    x11vnc -display :99 -forever -shared -passwd wings64 -rfbport 5900 -quiet -bg -o /tmp/x11vnc.log
    exec flycast -config config:Debug.SerialConsoleEnabled=yes -config config:UseReios=yes \
        -config window:fullscreen=yes -config window:width=640 -config window:height=480 "$1" > build/emu-play.log 2>&1
' bash "$cdi" >/dev/null
for _ in $(seq 30); do nc -z 127.0.0.1 5900 2>/dev/null && break; sleep 0.5; done
echo "emu_play: Flycast running; opening Screen Sharing (password: wings64). Stop: docker rm -f wings64-play"
open "vnc://:wings64@localhost:5900"
