#!/usr/bin/env bash
# gfxstat.sh — median / max display-list time and frames per second per game state from a serial or SD log
# (the "state:", "gfx: wait .. dl .." and "game: t .. gfx N" lines). Usage: scripts/gfxstat.sh [log]
set -euo pipefail
log=${1:-build/emu-serial.log}
python3 -I - "$log" <<'PY'
import re, statistics, sys
names = {0: "title", 2: "test details", 3: "pilot select", 4: "test setup", 5: "flight", 6: "results",
         10: "file menu", 11: "class select", 12: "test overview", 14: "congrats"}
st, last, fps, dl = -1, None, {}, {}
for l in open(sys.argv[1], "rb").read().decode("latin-1").splitlines():
    m = re.match(r"state: (-?\d+)", l)
    if m: st = int(m.group(1))
    m = re.match(r"game: t \d+ s gfx (\d+)", l)
    if m:
        g = int(m.group(1))
        if last is not None and g > last: fps.setdefault(st, []).append(g - last)
        last = g
    m = re.match(r"gfx: wait \d+ us, dl (\d+)", l)
    if m: dl.setdefault(st, []).append(int(m.group(1)))
print(f"{'state':14} {'fps':>5} {'dl med ms':>9} {'dl max':>7} {'n':>4}")
for s in sorted(set(fps) | set(dl)):
    f = fps.get(s, []); d = dl.get(s, [])
    print(f"{names.get(s, str(s)):14} {sum(f)/len(f) if f else 0:5.1f} {statistics.median(d)/1000 if d else 0:9.2f} "
          f"{max(d)/1000 if d else 0:7.2f} {len(d):4}")
PY
