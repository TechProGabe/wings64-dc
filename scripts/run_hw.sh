#!/usr/bin/env bash
# run_hw.sh — no dcload cable exists (PLAN §5): "run on hardware" means burn this image.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cdi=${1:-$ROOT/build/wings64.cdi}
[ -f "$cdi" ] || { echo "run_hw: $cdi missing (./run.sh hwdisc first)" >&2; exit 1; }
cat <<MSG
Burn this image to CD-R (DiscJuggler/ImgBurn-style raw DAO; slowest reliable speed):

    $cdi
    build $(cat "$ROOT/disc/disc.id" 2>/dev/null || echo '?')   sha256 $(shasum -a 256 "$cdi" | cut -c1-16)…

Checklist for the retail console (composite/S-video, 480i):
  1. Disc boots past the BIOS logo (record console model/revision if visible).
  2. The milestone screen shows the expected lines (see kb/STATE.md for this milestone's exit criteria).
  3. Photograph the screen; note load time and any red line.
  4. SD adapter attached (MBR FAT32, 4 KB clusters); hwdisc images log to SD from boot (settings menu switches it):
     copy /sd/wings64/log-NNN.txt and receipt.txt (docs/testing.md "SD card log").
  5. Audio (M4, docs/audio-design.md; the emulator gate cannot hear, so these are the first real checks):
     a. Boot screen line "audio pool 1816 KB, 164 waves": note the boot time it adds (~2 s in Flycast).
     b. Title music within ~2 s of the title; menu cursor/select SFX; the ~1 s pause at title music
        changes is uvaSeqStop waiting for note releases (N64 behaviour, not a hang).
     c. Rocket Belt flight: engine SFX + music together; listen for clicks at loop points, crackle on
        high-pitched SFX, tempo drift, a sound that cuts out or never stops.
     d. Leave it in attract mode 10 min: no silence, no dropouts. With the SD log on, the "aud: t" lines
        (every 5 s) should show rate ~22050 Hz, +300 frames, underruns flat, misses 0.
     e. Waves that sound bad: the log's "aud: start ... wave N (sfx:0x...)" names them; that bank:base
        goes in tools/audio/pcm_force.txt (PCM8) for the next disc.
  6. Record the result in kb/state-log.md (date, build id, console, video, SD log, observations).
MSG
