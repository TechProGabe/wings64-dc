#!/usr/bin/env bash
# emu_smoke.sh — boot an image in Flycast (host, macOS), capture the serial console, and pass when
# the milestone marker appears. Flycast has no headless/auto-exit mode, so it is killed afterwards.
# A pass here is a smoke test only; hardware is truth (CLAUDE.md).
#   emu_smoke.sh [cdi] [marker] [timeout_s]
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cdi=${1:-$ROOT/build/wings64-emu.cdi}
marker=${2:-WINGS64_M0_OK}
timeout=${3:-90}
fly=/Applications/Flycast.app/Contents/MacOS/Flycast
log=$ROOT/build/emu-serial.log
[ -x "$fly" ] || { echo "emu_smoke: Flycast not found at $fly" >&2; exit 2; }
[ -f "$cdi" ] || { echo "emu_smoke: $cdi missing (./run.sh disc --emu)" >&2; exit 2; }
"$fly" -config config:Debug.SerialConsoleEnabled=yes ${FLYCAST_ARGS:-} "$cdi" > "$log" 2>&1 &
pid=$!
rc=1
for _ in $(seq "$timeout"); do
    if grep -q "$marker" "$log"; then rc=0; break; fi
    if grep -qE 'WINGS64_M[0-9]+_FAIL|kernel panic|assertion failed|Unhandled exception' "$log"; then break; fi
    kill -0 $pid 2>/dev/null || break
    sleep 1
done
kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null
grep -aE 'wings64|assetroot|romfs|WINGS64_|db:|panic|assert|exception' "$log" | head -40
if [ $rc = 0 ]; then echo "emu_smoke: PASS ($marker)"; else echo "emu_smoke: FAIL (no $marker in ${timeout}s; full log $log)"; fi
exit $rc
