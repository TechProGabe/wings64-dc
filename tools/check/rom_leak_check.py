#!/usr/bin/env python3
"""rom_leak_check: make sure no ROM or disc material is tracked by git.

Files come from `git ls-files -z` (cwd = repo root). If git is unavailable,
the tree is walked instead, skipping .git, build/, build-dc/, build-host/,
external/ and disc/. Only regular files are checked (no symlinks, no
submodule directories).

Rules:
  1. Path rules, case-insensitive: suffix .z64 .n64 .v64 .cdi .iso .gdi .fs, images (.png .jpg ...)
     .bin.scr; basename starting "baserom"; basename 1ST_READ.BIN,
     adpcm_pool.bin, sample_table.bin or wings64.fs; path under disc/ or build/.
     Path rules apply to everything, including external/.
  2. Size: no tracked file larger than 1 MiB. external/ is exempt.
  3. Magic: no file beginning with the N64 ROM header bytes 80 37 12 40.
  4. Internal name: no binary file with "Pilot Wings64" within 64 bytes of
     "NPWE" (text docs describing the header are allowed).
  5. Text paths: no absolute filesystem path to a *.z64 file (absolute means
     the token starts with /, ~ or a drive letter; ./x.z64, ../x.z64 and
     docs/x.z64 are relative and allowed; bare baserom.z64 is allowed), and no
     "Downloads/Pilotwings" anywhere in a text file.

external/ is exempt from rules 2-5 (size and binary/content detection); its
paths are still checked by rule 1. The SHA-1 ec771aedf54ee1b214c25404fb4ec51cfd43191a
is provenance, not ROM data, and is allowed anywhere. This script skips
content scans of itself, since it necessarily spells out the patterns.
"""
import argparse
import os
import re
import stat
import subprocess
import sys
from pathlib import Path

NAME = "rom_leak_check"
SELF = Path(__file__).resolve()
MIB = 1024 * 1024
BAD_SUFFIXES = (".z64", ".n64", ".v64", ".cdi", ".iso", ".gdi", ".fs", ".bin.scr",
                # screenshots/textures render ROM assets: evidence stays in untracked build/evidence/
                ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".ppm")
BAD_BASENAMES = {"1st_read.bin", "adpcm_pool.bin", "sample_table.bin", "wings64.fs"}
SKIP_TOP = {"build", "build-dc", "build-host", "external", "disc"}
N64_MAGIC = b"\x80\x37\x12\x40"
NAME_A = b"Pilot " b"Wings64"
NAME_B = b"NPWE"
ABS_ROM_RE = re.compile(r"[/\\][^\s'\"]*\.z64")
DOWNLOADS_RE = "Downloads/Pilotwings"


def tracked_files(root):
    try:
        out = subprocess.run(
            ["git", "ls-files", "-z"], cwd=root, capture_output=True, check=True
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return walk_tree(root)
    return [p for p in out.decode("utf-8", "surrogateescape").split("\0") if p]


def walk_tree(root):
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        here = Path(dirpath)
        if here == root:
            dirnames[:] = [d for d in dirnames if d not in SKIP_TOP]
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for fn in filenames:
            found.append((here / fn).relative_to(root).as_posix())
    return found


def path_problems(rel):
    low = rel.lower()
    base = low.rsplit("/", 1)[-1]
    msgs = []
    if low.endswith(BAD_SUFFIXES):
        msgs.append("tracked path has forbidden ROM/disc suffix")
    if base.startswith("baserom"):
        msgs.append("tracked path is a baserom file")
    if base in BAD_BASENAMES:
        msgs.append("tracked path is a forbidden ROM-derived filename")
    if low.startswith(("disc/", "build/")):
        msgs.append("tracked path is under disc/ or build/")
    return [f"{rel}:1: {m}" for m in msgs]


def content_problems(rel, data):
    msgs = []
    if data[:4] == N64_MAGIC:
        msgs.append(f"{rel}:1: file starts with N64 ROM magic 80371240")
    binary = b"\x00" in data[:8192]
    if binary:
        # Internal-name check is for embedded ROM bytes only; a text doc that
        # describes the header (docs/rom-provenance.md) is provenance, not data.
        a_hits = [m.start() for m in re.finditer(re.escape(NAME_A), data)]
        b_hits = [m.start() for m in re.finditer(re.escape(NAME_B), data)]
        for off in a_hits:
            if any(abs(off - b) <= 64 for b in b_hits):
                line = data.count(b"\n", 0, off) + 1
                msgs.append(f"{rel}:{line}: N64 internal name 'Pilot Wings64' next to 'NPWE' (ROM header)")
                break
        return msgs  # binary: text rules do not apply
    text = data.decode("utf-8", errors="replace")
    for lineno, line in enumerate(text.splitlines(), 1):
        if DOWNLOADS_RE in line:
            msgs.append(f"{rel}:{lineno}: reference to ROM download location '{DOWNLOADS_RE}'")
        for m in ABS_ROM_RE.finditer(line):
            j = m.start()
            while j > 0 and not line[j - 1].isspace() and line[j - 1] not in "'\"":
                j -= 1
            tok = line[j:m.end()]
            if tok.startswith("."):
                continue  # relative, e.g. ./baserom.z64
            if tok.startswith(("/", "\\", "~")) or re.match(r"^[A-Za-z]:[\\/]", tok):
                msgs.append(f"{rel}:{lineno}: absolute path to a .z64 ROM file: {tok}")
    return msgs


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    args = ap.parse_args()
    root = Path(args.root).resolve()

    problems = []
    scanned = 0
    for rel in tracked_files(root):
        path = root / rel
        try:
            st = os.lstat(path)
        except OSError:
            continue
        if not stat.S_ISREG(st.st_mode):
            continue
        scanned += 1
        problems += path_problems(rel)
        if rel.startswith("external/"):
            continue
        if st.st_size > MIB:
            problems.append(f"{rel}:1: file is {st.st_size} bytes (over 1 MiB limit)")
        if path.resolve() == SELF:
            continue
        data = path.read_bytes()
        problems += content_problems(rel, data)

    for p in problems:
        print(p)
    if problems:
        print(f"{NAME}: FAIL ({len(problems)} problems)")
        return 1
    print(f"{NAME}: OK ({scanned} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
