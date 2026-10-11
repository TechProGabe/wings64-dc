#!/usr/bin/env python3
"""diverges_check: every DIVERGES marker in port source must state a reason.

Scans *.c *.h *.S *.s *.py *.sh under src/, tools/ and scripts/ (whichever
exist). A marker is the whole word DIVERGES. The only valid form is
`DIVERGES: <reason>`, where the reason has at least 8 non-space characters on
the same line after the colon (a trailing comment closer such as */ or -->
does not count). A bare DIVERGES, a DIVERGES without a colon, or a reason
that is too short is flagged. This script ignores itself.
"""
import argparse
import os
import re
import sys
from pathlib import Path

NAME = "diverges_check"
SELF = Path(__file__).resolve()
SCAN_DIRS = ("src", "tools", "scripts")
EXTS = (".c", ".h", ".S", ".s", ".py", ".sh")
MARKER_RE = re.compile(r"\bDIVERGES\b(.*)$")
CLOSERS_RE = re.compile(r"(\*/|-->)\s*$")
MIN_REASON = 8


def source_files(root):
    for top in SCAN_DIRS:
        base = root / top
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if not d.startswith(".") and d != "__pycache__"]
            for fn in sorted(filenames):
                if fn.endswith(EXTS):
                    yield Path(dirpath) / fn


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    args = ap.parse_args()
    root = Path(args.root).resolve()

    problems = []
    scanned = 0
    for path in source_files(root):
        if path.resolve() == SELF:
            continue
        scanned += 1
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            for m in MARKER_RE.finditer(line):
                rest = m.group(1)
                if not rest.startswith(":"):
                    problems.append(f"{rel}:{lineno}: DIVERGES marker without ':' reason (use 'DIVERGES: <reason>')")
                    continue
                reason = CLOSERS_RE.sub("", rest[1:])
                if len(re.sub(r"\s", "", reason)) < MIN_REASON:
                    problems.append(f"{rel}:{lineno}: DIVERGES reason too short (need >= {MIN_REASON} non-space chars)")

    for p in problems:
        print(p)
    if problems:
        print(f"{NAME}: FAIL ({len(problems)} problems)")
        return 1
    print(f"{NAME}: OK ({scanned} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
