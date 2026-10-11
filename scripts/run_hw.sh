#!/usr/bin/env bash
# run_hw.sh — no dcload cable exists (PLAN §5): "run on hardware" means burn this image.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cdi=${1:-$ROOT/build/wings64.cdi}
[ -f "$cdi" ] || { echo "run_hw: $cdi missing (./run.sh disc first)" >&2; exit 1; }
cat <<MSG
Burn this image to CD-R (DiscJuggler/ImgBurn-style raw DAO; slowest reliable speed):

    $cdi
    build $(cat "$ROOT/disc/disc.id" 2>/dev/null || echo '?')   sha256 $(shasum -a 256 "$cdi" | cut -c1-16)…

Checklist for the retail console (composite/S-video, 480i):
  1. Disc boots past the BIOS logo (record console model/revision if visible).
  2. The milestone screen shows the expected lines (see kb/STATE.md for this milestone's exit criteria).
  3. Photograph the screen; note load time and any red line.
  4. SD adapter attached (MBR FAT32, 4 KB clusters) and SD logging on (settings menu or DB_LOG_SD=1):
     copy /sd/wings64/log-NNN.txt and receipt.txt (docs/testing.md "SD card log").
  5. Record the result in kb/state-log.md (date, build id, console, video, SD log, observations).
MSG
