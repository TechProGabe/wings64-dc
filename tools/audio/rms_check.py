#!/usr/bin/env python3
"""rms_check.py -- read a Flycast audio capture (scripts/emu_smoke.sh EMU_AUDIO=1) and report loudness.

The capture is raw S16LE stereo at 44.1 kHz (SDL disk driver). Prints the RMS level per second (dBFS),
the share of 100 ms windows that are silent (< -60 dBFS), the longest silent run, and clipped samples.
With --gap-max S, exits 1 when a silent run inside [--from, --to] seconds is longer than S seconds
(the dropout check for a stretch where the game is known to play sound).
Usage: python3 -I tools/audio/rms_check.py build/emu-audio.raw [--rate 44100] [--from 0] [--to 1e9]
       [--gap-max 0.5]
"""
import argparse
import math
import struct
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raw")
    ap.add_argument("--rate", type=int, default=44100)
    ap.add_argument("--from", dest="t0", type=float, default=0.0)
    ap.add_argument("--to", dest="t1", type=float, default=1e9)
    ap.add_argument("--gap-max", type=float, default=None)
    a = ap.parse_args()
    data = open(a.raw, "rb").read()
    n = len(data) // 4
    if n == 0:
        print("rms_check: empty capture")
        return 1
    win = a.rate // 10
    silent_db = -60.0
    rows, clipped = [], 0
    for w in range(n // win):
        frames = struct.unpack_from(f"<{win * 2}h", data, w * win * 4)
        acc = 0
        for v in frames:
            acc += v * v
            if v in (32767, -32768):
                clipped += 1
        rms = math.sqrt(acc / len(frames))
        rows.append(20 * math.log10(rms / 32768) if rms > 0 else -120.0)
    secs = len(rows) / 10
    print(f"rms_check: {secs:.1f} s captured, {clipped} clipped samples")
    line = []
    for s in range(0, len(rows), 10):
        sec = rows[s:s + 10]
        p = sum(10 ** (d / 10) for d in sec) / len(sec)
        line.append(f"{10 * math.log10(p) if p > 0 else -120:.0f}")
    print("  dBFS per second: " + " ".join(line))
    silent = sum(1 for d in rows if d < silent_db)
    print(f"  silent 100 ms windows: {silent}/{len(rows)} ({100 * silent / len(rows):.1f}%)")
    longest, cur, start, worst_at = 0, 0, 0, 0
    for i, d in enumerate(rows):
        t = i / 10
        if a.t0 <= t <= a.t1 and d < silent_db:
            if cur == 0:
                start = t
            cur += 1
            if cur > longest:
                longest, worst_at = cur, start
        else:
            cur = 0
    print(f"  longest silent run in [{a.t0:g}, {min(a.t1, secs):g}] s: {longest / 10:.1f} s at {worst_at:.1f} s")
    if a.gap_max is not None and longest / 10 > a.gap_max:
        print(f"rms_check: FAIL (silent run > {a.gap_max} s)")
        return 1
    print("rms_check: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
