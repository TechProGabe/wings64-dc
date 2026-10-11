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
cmd_emu() { ## native Flycast on the Mac (~/Applications/Flycast-wings64.app, docs/flycast-macos.md)
    local cdi=${1:-build/wings64-emu.cdi}
    local fly=${FLYCAST_BIN:-$HOME/Applications/Flycast-wings64.app/Contents/MacOS/Flycast}
    [ -x "$fly" ] || { echo "emu: $fly missing; build it per docs/flycast-macos.md, or use ./run.sh play" >&2; exit 1; }
    mkdir -p "${TMPDIR:-/tmp}/wings64-flycast"
    # Run outside the repo: a crashing emulator drops core files in its working directory.
    (cd "${TMPDIR:-/tmp}/wings64-flycast" && "$fly" -config config:Debug.SerialConsoleEnabled=yes -config config:UseReios=yes \
        "$ROOT/$cdi" > "$ROOT/build/emu-native.log" 2>&1 &)
    echo "emu: Flycast started (log build/emu-native.log)"
}
cmd_play() { ## watch/play the emu image on the Mac (Flycast in the container, VNC -> Screen Sharing)
    scripts/emu_play.sh "$@"
}
cmd_gate() { ## the milestone gate: build the gate image (scripted input) and run it in the emulator
    local m script
    m=$(sed -n 's/^#define WINGS64_MILESTONE \([0-9]*\).*/\1/p' src/dc/milestone.h)
    case "$m" in
        2) script='8000:S:200,16000:A:200,30000:A:200' ;;
        # M3: title -> Start game -> file 1 -> class select (stick right: rocket belt) -> pilot -> overview ->
        #     details -> start; no stick input: the test ends on its own and shows results.
        *) script='8000:S:200,16000:A:200,30000:A:200,37000:>:300,40000:A:200,46000:A:200,52000:A:200,58000:A:200,70000:A:2500' ;;
    esac
    # GATE_CFLAGS: extra debug knobs for a traced gate run, e.g. GATE_CFLAGS=-DDB_GFX_TRACE=1
    EXTRA_CFLAGS="-DDB_AUTOPLAY=1 -DDB_AUTOPLAY_SCRIPT=\"\\\"$script\\\"\" ${GATE_CFLAGS:-}" cmd_build
    $CTR run scripts/make_cdi.sh --emu
    EMU_RECORD=${EMU_RECORD:-100} scripts/emu_smoke.sh build/wings64-emu.cdi "" 150
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
