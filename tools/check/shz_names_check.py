#!/usr/bin/env python3
"""shz_names_check: lint the port's use of the sh4zam math library.

Rules (applied to every *.c / *.h under src/, except src/dc/test/hoststubs/):

1. Unknown sh4zam names: every `shz_*` / `SHZ_*` identifier used in port
   source must occur somewhere in the sh4zam headers
   (external/sh4zam/include/**/*.h). Names the file itself #defines, and the
   configuration macros SHZ_BACKEND, SHZ_TLS_MODEL, SHZ_SH4, SHZ_SW and
   SHZ_TLS_IMPLICIT, are always allowed. If the submodule is not checked out
   the script prints SKIP and exits 0.

2. Forbidden sh4zam names: a fixed list of names that do not exist per the
   project mandate (shz_memset, shz_macw, shz_fast_invf, ...). Always reported,
   even if a header happens to mention them. shz_memset8 and shz_memset2_16
   are real and are not on the list (matching is exact-token).

3. KOS matrix API: `mat_load`, `mat_identity`, `mat_trans_single*`, etc. and
   `#include <dc/matrix.h>` / `#include <dc/matrix3d.h>` are forbidden in port
   source because the KOS matrix code clobbers the SH4 XMTRX registers. Use
   sh4zam instead.

4. No `double` under src/dc/: the build is -m4-single-only. A file may opt out
   with a line comment containing `shz-lint: allow-double`.

Comments and string/char literals are stripped before token matching, so names
mentioned in comments or strings are ignored. Line numbers are preserved.

Usage: shz_names_check.py [--root DIR]
Exit status: 0 pass (or SKIP), 1 violations found.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

IDENT_RE = re.compile(r'\b(shz_[A-Za-z0-9_]+|SHZ_[A-Za-z0-9_]+)\b')
DEFINE_RE = re.compile(r'#\s*define\s+(SHZ_\w+|shz_\w+)')
ALWAYS_ALLOW = frozenset({
    'SHZ_BACKEND', 'SHZ_TLS_MODEL', 'SHZ_SH4', 'SHZ_SW', 'SHZ_TLS_IMPLICIT',
})
FORBIDDEN = frozenset({
    'shz_memset', 'shz_memset2', 'shz_memset4', 'shz_memset32', 'shz_macw',
    'shz_xmtrx_trans_vec4', 'shz_xmtrx_load_4x4_wxyz', 'shz_xmtrx_angles',
    'shz_xmtrx_position', 'shz_xmtrx_size', 'shz_fast_invf', 'shz_matrix_4x4_t',
})
KOS_RE = re.compile(
    r'\bmat_(load|store|identity|apply|transform|trans_single\w*|trans_nodiv\w*'
    r'|perspective|lookat|rotate\w*|translate|scale)\b')
KOS_INCLUDE_RE = re.compile(r'#\s*include\s*[<"]dc/matrix(3d)?\.h[>"]')
INCLUDE_LINE_RE = re.compile(r'\s*#\s*include\b')
DOUBLE_RE = re.compile(r'\bdouble\b')
ALLOW_DOUBLE_RE = re.compile(r'//[^\n]*shz-lint: allow-double')
KOS_MSG = 'KOS mat_* forbidden (XMTRX clobber); use sh4zam'
DOUBLE_MSG = 'double forbidden (-m4-single-only)'


def _blank(s: str) -> str:
    """Replace every non-newline character with a space."""
    return re.sub(r'[^\n]', ' ', s)


def strip_c(text: str) -> tuple[str, str]:
    """Return (no_comments, code).

    no_comments: comments blanked, string/char literals kept (used for
                 #include and #define matching).
    code:        comments and string/char literals blanked (token matching).
    Newlines are always preserved so line numbers stay correct.
    """
    nc: list[str] = []
    st: list[str] = []
    i, n = 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == '//':
            j = text.find('\n', i)
            j = n if j < 0 else j
            blank = _blank(text[i:j])
            nc.append(blank)
            st.append(blank)
            i = j
            continue
        if two == '/*':
            j = text.find('*/', i + 2)
            end = n if j < 0 else j + 2
            blank = _blank(text[i:end])
            nc.append(blank)
            st.append(blank)
            i = end
            continue
        if text[i] in '"\'':
            quote = text[i]
            j = i + 1
            while j < n and text[j] != quote and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            j = min(j, n)
            end = j + 1 if j < n and text[j] == quote else j
            seg = text[i:end]
            nc.append(seg)
            st.append(_blank(seg))
            i = end
            continue
        nc.append(text[i])
        st.append(text[i])
        i += 1
    return ''.join(nc), ''.join(st)


def read_text(path: Path) -> str:
    return path.read_text(encoding='utf-8', errors='replace')


def collect_known(include_dir: Path) -> set[str]:
    known: set[str] = set()
    for header in sorted(include_dir.rglob('*.h')):
        known.update(IDENT_RE.findall(read_text(header)))
    return known


def collect_sources(root: Path) -> list[Path]:
    src = root / 'src'
    if not src.is_dir():
        return []
    files = [p for p in list(src.rglob('*.c')) + list(src.rglob('*.h')) if p.is_file()]
    kept = []
    for p in sorted(files):
        rel_parts = p.relative_to(root).parts
        if rel_parts[:4] == ('src', 'dc', 'test', 'hoststubs'):
            continue
        kept.append(p)
    return kept


def check_file(path: Path, root: Path, known: set[str]) -> list[tuple[str, int, str]]:
    rel = path.relative_to(root).as_posix()
    raw = read_text(path)
    no_comments, code = strip_c(raw)
    problems: list[tuple[str, int, str]] = []

    allowed = set(DEFINE_RE.findall(no_comments)) | ALWAYS_ALLOW
    in_dc = rel.startswith('src/dc/')
    allow_double = bool(ALLOW_DOUBLE_RE.search(raw))

    for lineno, line in enumerate(no_comments.split('\n'), 1):
        if KOS_INCLUDE_RE.search(line):
            problems.append((rel, lineno, KOS_MSG))

    for lineno, line in enumerate(code.split('\n'), 1):
        if INCLUDE_LINE_RE.match(line):
            continue  # header file names in #include are not API names
        seen: set[str] = set()
        for name in IDENT_RE.findall(line):
            if name in seen:
                continue
            seen.add(name)
            if name in FORBIDDEN:
                problems.append((rel, lineno,
                                 f"forbidden sh4zam name '{name}' (does not exist)"))
            elif name not in known and name not in allowed:
                problems.append((rel, lineno, f"unknown sh4zam name '{name}'"))
        if KOS_RE.search(line):
            problems.append((rel, lineno, KOS_MSG))
        if in_dc and not allow_double and DOUBLE_RE.search(line):
            problems.append((rel, lineno, DOUBLE_MSG))

    return problems


def main(argv: list[str] | None = None) -> int:
    default_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description='Lint sh4zam name usage in the port.')
    parser.add_argument('--root', default=str(default_root),
                        help='repo root (default: two dirs above this script)')
    args = parser.parse_args(argv)
    root = Path(args.root).resolve()

    include_dir = root / 'external' / 'sh4zam' / 'include'
    if not include_dir.is_dir():
        print('shz_names_check: SKIP (sh4zam submodule not checked out)')
        return 0

    known = collect_known(include_dir)
    sources = collect_sources(root)

    problems: list[tuple[str, int, str]] = []
    for path in sources:
        problems.extend(check_file(path, root, known))
    problems.sort(key=lambda t: (t[0], t[1]))

    for rel, lineno, msg in problems:
        print(f'{rel}:{lineno}: {msg}')

    if problems:
        print(f'shz_names_check: FAIL ({len(problems)} problems)')
        return 1
    print(f'shz_names_check: OK ({len(sources)} files, {len(known)} sh4zam names known)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
