#!/usr/bin/env bash
# ctr.sh — build the wings64-dc container image or run a command inside it.
#   scripts/ctr.sh build            build image wings64-dc:<pin hash> (tagged :dev too)
#   scripts/ctr.sh run <cmd...>     run <cmd> in /work with the repo bind-mounted, KOS env sourced
#   scripts/ctr.sh tag              print the image tag the pins resolve to
#   scripts/ctr.sh emu-build        build the Flycast emulator image (docker/emu/Dockerfile)
# WINGS64_NATIVE=1 skips the container and runs <cmd> on the host (KOS env must already be sourced).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck disable=SC1091
. "$ROOT/docker/pins.env"

# Tags hash only the inputs of each image: toolchain pins (no FLYCAST_/EMU_ lines, no comments) vs emulator pins.
pin_hash() { { grep -vE '^(#|$|FLYCAST_|EMU_)' "$ROOT/docker/pins.env"; cat "$ROOT/docker/Dockerfile" "$ROOT/docker/fetch-src.sh"; } | shasum -a 256 | cut -c1-12; }
emu_hash() { { grep -E '^FLYCAST_' "$ROOT/docker/pins.env"; cat "$ROOT/docker/emu/Dockerfile"; } | shasum -a 256 | cut -c1-12; }
TAG="$IMAGE_NAME:$(pin_hash)"

ensure_docker() {
    if ! docker info >/dev/null 2>&1; then
        if command -v colima >/dev/null 2>&1; then
            echo "ctr: starting colima" >&2
            colima start --cpu 6 --memory 8 >&2
        else
            echo "ctr: docker is not running" >&2; exit 1
        fi
    fi
}

cmd=${1:-}; shift || true
case "$cmd" in
    tag) echo "$TAG" ;;
    emu-tag) echo "$EMU_IMAGE_NAME:$(emu_hash)" ;;
    emu-build)
        ensure_docker
        etag="$EMU_IMAGE_NAME:$(emu_hash)"
        docker build --build-arg FLYCAST_URL="$FLYCAST_URL" --build-arg FLYCAST_TAG="$FLYCAST_TAG" \
            --build-arg FLYCAST_REV="$FLYCAST_REV" -t "$etag" -t "$EMU_IMAGE_NAME:dev" "$@" "$ROOT/docker/emu"
        ;;
    build)
        ensure_docker
        args=()
        for k in KOS_URL KOS_REV TOOLCHAIN_PROFILE MKDCDISC_URL MKDCDISC_REV KOS_SH4_PRECISION; do
            args+=(--build-arg "$k=${!k}")
        done
        docker build "${args[@]}" -t "$TAG" -t "$IMAGE_NAME:dev" "$@" "$ROOT/docker"
        ;;
    run)
        if [ "${WINGS64_NATIVE:-0}" = 1 ]; then cd "$ROOT"; exec "$@"; fi
        ensure_docker
        if ! docker image inspect "$TAG" >/dev/null 2>&1; then
            echo "ctr: image $TAG missing; run ./run.sh setup" >&2; exit 1
        fi
        tty=(); [ -t 0 ] && [ -t 1 ] && tty=(-it)
        exec docker run --rm ${tty[@]+"${tty[@]}"} -v "$ROOT:/work" -w /work \
            -e WINGS64_IN_CTR=1 -e RELEASE -e EXTRA_CFLAGS -e V \
            --user "$(id -u):$(id -g)" -e HOME=/tmp \
            "$TAG" bash -c '. /opt/toolchains/dc/kos/environ.sh && exec "$@"' bash "$@"
        ;;
    *) sed -n '2,7p' "$0" >&2; exit 2 ;;
esac
