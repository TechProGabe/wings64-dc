#!/usr/bin/env bash
# dc_env.sh — sourced by build scripts. Inside the container KOS's environ.sh is already sourced
# by scripts/ctr.sh; natively (WINGS64_NATIVE=1) source $KOS_BASE/environ.sh yourself, or set
# KOS_ENV to its path in scripts/dc_env.local.sh (gitignored).
ROOT=${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
[ -f "$ROOT/scripts/dc_env.local.sh" ] && . "$ROOT/scripts/dc_env.local.sh"
if [ -z "${KOS_BASE:-}" ] && [ -n "${KOS_ENV:-}" ]; then . "$KOS_ENV"; fi
if [ -z "${KOS_BASE:-}" ]; then
    echo "dc_env: KOS environment not loaded (use ./run.sh, or set KOS_ENV for --native)" >&2
    exit 1
fi
if [ "${KOS_SH4_PRECISION:-}" != "-m4-single-only" ]; then
    echo "dc_env: KOS_SH4_PRECISION='${KOS_SH4_PRECISION:-}' but the port needs -m4-single-only" >&2
    exit 1
fi
# Build id: <git short sha>[d if dirty]-<MMDD-HHMM UTC>. Used in disc.id and SD log names.
wings64_build_id() {
    local sha dirty
    sha=$(git -C "$ROOT" -c safe.directory='*' rev-parse --short=7 HEAD 2>/dev/null || echo nogit)
    dirty=$(git -C "$ROOT" -c safe.directory='*' status --porcelain --untracked-files=no 2>/dev/null | head -1)
    echo "${sha}${dirty:+d}-$(date -u +%m%d-%H%M)"
}
