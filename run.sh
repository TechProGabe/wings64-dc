#!/usr/bin/env bash
# run.sh — wings64-dc dispatcher. Every build step runs in the pinned container (docker/pins.env).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT"
CTR=scripts/ctr.sh
[ "${1:-}" = --native ] && { export WINGS64_NATIVE=1; shift; }

cmd_setup() { ## container image, submodules, decomp fetch, git hooks
    git submodule update --init external/sh4zam
    $CTR build
    scripts/fetch_decomp.sh
    git config core.hooksPath .githooks
}
cmd_build() { ## build/wings64.elf
    $CTR run scripts/build_target.sh "$@"
}
cmd_disc() { ## build/wings64.cdi (padding on, CD-R); --emu: wings64-emu.cdi (-N); --data-data: -L data-data
    cmd_build
    $CTR run scripts/make_cdi.sh "$@"
}
cmd_release() { ## build/wings64-release.cdi and -dd variant (no db, NDEBUG). Local only, never CI.
    [ -z "${EXTRA_CFLAGS:-}" ] || { echo "release: unset EXTRA_CFLAGS" >&2; exit 1; }
    cmd_test --ci
    export RELEASE=1
    $CTR run scripts/build_target.sh
    $CTR run scripts/make_cdi.sh
    $CTR run scripts/make_cdi.sh --data-data
}
cmd_test() { ## tree checks; without --ci also ROM slice, ILP32 host tests, decomp purity (container)
    scripts/test_tree.sh
    if [ "${1:-}" != --ci ]; then
        python3 -I tools/rom/slice_fs.py --quiet
        $CTR run scripts/test_host.sh --rom
        $CTR run make -f src/game/Makefile -j6 purity
    fi
}
cmd_run-hw() { ## print the .cdi to burn and the hardware checklist
    scripts/run_hw.sh "$@"
}
cmd_emu() { ## boot build/wings64-emu.cdi in Flycast with the serial console on
    local cdi=${1:-build/wings64-emu.cdi}
    /Applications/Flycast.app/Contents/MacOS/Flycast -config config:Debug.SerialConsoleEnabled=yes "$ROOT/$cdi"
}
cmd_smoke() { ## Flycast smoke: boot the emu image, wait for the milestone marker on serial
    scripts/emu_smoke.sh "$@"
}
cmd_decomp-refresh() { ## re-fetch the decomp at DECOMP_REV and re-apply patches/
    scripts/fetch_decomp.sh --force
}
cmd_clean() { ## remove build outputs (keeps disc/ slice)
    rm -rf build
}
cmd_shell() { ## interactive shell in the container
    $CTR run bash
}
help() {
    echo "usage: ./run.sh [--native] <command> [args]"
    grep -E '^cmd_[a-z-]+\(\) \{ ##' "$0" | sed -E 's/^cmd_([a-z-]+)\(\) \{ ## (.*)/  \1|\2/' | column -t -s'|'
}
c=${1:-help}; shift || true
if declare -F "cmd_$c" >/dev/null; then "cmd_$c" "$@"; else help; [ "$c" = help ]; fi
