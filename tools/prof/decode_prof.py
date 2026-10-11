#!/usr/bin/env python3
"""decode_prof.py -- flat profile from a hardware PROFnnn.BIN (src/dc/prof.c) and the ELF it ran.

Maps every 16-byte PC bin to the function containing it (symbols from sh-elf-nm, run in the pinned
container unless --nm FILE gives a saved `nm -n` listing) and prints the top functions by samples.
The ELF must be the one the disc was built from (the NAS .src.json records its sha256).
Usage: python3 -I tools/prof/decode_prof.py PROF000.BIN path/to/wings64.elf [--top 40] [--nm nm.txt]
       python3 -I tools/prof/decode_prof.py --serial build/emu-serial.log path/to/wings64.elf
       (Flycast: the "prof: bin" lines logged per dump window, one table per window)
"""
import argparse
import bisect
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def symbols(elf, nm_file):
    if nm_file:
        text = Path(nm_file).read_text()
    else:
        rel = Path(elf).resolve()
        try:
            rel = rel.relative_to(ROOT)
        except ValueError:
            sys.exit("decode_prof: the ELF must be inside the repo for the container (or pass --nm)")
        text = subprocess.run([str(ROOT / "scripts/ctr.sh"), "run", "sh-elf-nm", "-n", "--defined-only", str(rel)],
                              capture_output=True, text=True, check=True).stdout
    syms = []
    for ln in text.splitlines():
        parts = ln.split()
        if len(parts) >= 3 and parts[1] in "tTwW":
            syms.append((int(parts[0], 16), parts[2]))
    syms.sort()
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("--serial", action="store_true", help="bin is a serial log with 'prof: bin' lines")
    ap.add_argument("elf")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--nm")
    a = ap.parse_args()
    if a.serial:
        return serial(a)
    data = Path(a.bin).read_bytes()
    magic, ver, lo, shift, n, samples, outside, ms = struct.unpack_from("<8I", data, 0)
    if magic != 0x46525057 or ver != 1:
        sys.exit("decode_prof: not a WPRF v1 file")
    bins = struct.unpack_from(f"<{n}I", data, 32)
    syms = symbols(a.elf, a.nm)
    addrs = [s[0] for s in syms]
    per = {}
    for i, c in enumerate(bins):
        if not c:
            continue
        pc = lo + (i << shift)
        k = bisect.bisect_right(addrs, pc) - 1
        name = syms[k][1] if k >= 0 else "?"
        per[name] = per.get(name, 0) + c
    inside = sum(bins)
    print(f"decode_prof: {samples} samples over {ms / 1000:.1f} s ({samples / max(ms, 1) * 1000:.0f} Hz), "
          f"{outside} outside .text")
    print(f"{'%':>6} {'samples':>8}  function")
    for name, c in sorted(per.items(), key=lambda kv: -kv[1])[:a.top]:
        print(f"{100 * c / max(inside, 1):6.2f} {c:8d}  {name}")


def serial(a):
    syms = symbols(a.elf, a.nm)
    addrs = [s[0] for s in syms]
    per, total = {}, 0
    for ln in Path(a.bin).read_bytes().decode("latin-1").splitlines():
        p = ln.split()
        if ln.startswith("prof: bin"):
            pc, c = int(p[2], 16), int(p[3])
            k = bisect.bisect_right(addrs, pc) - 1
            name = syms[k][1] if k >= 0 else "?"
            per[name] = per.get(name, 0) + c
        elif ln.startswith("prof: end"):
            total = int(p[-1])
            print(f"== window {' '.join(p[2:-1])}: {total} samples (top bins only)")
            for name, c in sorted(per.items(), key=lambda kv: -kv[1])[:a.top]:
                print(f"{100 * c / max(total, 1):6.2f} {c:8d}  {name}")
            per = {}


if __name__ == "__main__":
    main()
