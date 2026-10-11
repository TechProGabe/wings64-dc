#!/usr/bin/env bash
# build_target.sh — build build/wings64.elf (or wings64-release.elf with RELEASE=1). Runs in the container.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/scripts/dc_env.sh"
cd "$ROOT"
if [ ! -f external/sh4zam/include/sh4zam/shz_sh4zam.h ]; then
    echo "build_target: external/sh4zam missing; run: git submodule update --init" >&2; exit 1
fi
BUILD_ID=${BUILD_ID:-$(wings64_build_id)}
mkdir -p build
echo "$BUILD_ID" > build/build-id.txt
exec make -f src/game/Makefile -j"$(nproc 2>/dev/null || echo 4)" BUILD_ID="$BUILD_ID" "$@"
