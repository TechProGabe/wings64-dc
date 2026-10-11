#!/usr/bin/env bash
# test_host.sh — build and run the ILP32 host tests (src/dc/test) under qemu-arm. Runs in the container.
#   test_host.sh          unit tests only (no ROM data)
#   test_host.sh --rom    + host M0 boot check against disc/ (needs tools/rom/slice_fs.py output)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make -f src/dc/test/Makefile -j"$(nproc)"
args=()
[ "${1:-}" = --rom ] && args=(disc)
qemu-arm build/host/hosttest ${args[@]+"${args[@]}"}
