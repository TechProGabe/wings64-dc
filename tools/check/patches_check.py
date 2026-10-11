#!/usr/bin/env python3
"""patches_check: every patches/*.patch needs a notes sibling.

Rules for the patches/ directory (non-recursive):
  - each *.patch filename matches ^\\d{3}-[a-z0-9][a-z0-9-]*\\.patch$
  - each *.patch has a sibling <stem>.md that is non-empty
  - each *.md (except README.md) has a matching <stem>.patch
Passes with 0 files when patches/ is missing or has no .patch files.
"""
import argparse
import re
import sys
from pathlib import Path

NAME = "patches_check"
NAME_RE = re.compile(r"^\d{3}-[a-z0-9][a-z0-9-]*\.patch$")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    args = ap.parse_args()
    pdir = Path(args.root).resolve() / "patches"

    problems = []
    patches = []
    mds = []
    if pdir.is_dir():
        for entry in sorted(pdir.iterdir(), key=lambda p: p.name):
            if not entry.is_file():
                continue
            if entry.name.endswith(".patch"):
                patches.append(entry)
            elif entry.name.endswith(".md"):
                mds.append(entry)

    for p in patches:
        rel = f"patches/{p.name}"
        if not NAME_RE.match(p.name):
            problems.append(f"{rel}:1: filename must match NNN-kebab-name.patch")
        md = pdir / (p.name[: -len(".patch")] + ".md")
        if not md.is_file():
            problems.append(f"{rel}:1: missing notes file patches/{md.name}")
        elif not md.read_text(encoding="utf-8", errors="replace").strip():
            problems.append(f"patches/{md.name}:1: notes file is empty")

    patch_stems = {p.name[: -len(".patch")] for p in patches}
    for md in mds:
        if md.name == "README.md":
            continue
        if md.name[: -len(".md")] not in patch_stems:
            problems.append(f"patches/{md.name}:1: notes file has no matching .patch")

    for p in problems:
        print(p)
    if problems:
        print(f"{NAME}: FAIL ({len(problems)} problems)")
        return 1
    print(f"{NAME}: OK ({len(patches)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
