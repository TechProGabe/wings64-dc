#!/usr/bin/env bash
# fetch_decomp.sh — fetch the decomp at DECOMP_REV into external/pw64-decomp/ and apply patches/.
# Idempotent: re-fetches only if the stamp differs from DECOMP_REV + patch hash. --force refetches.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/docker/pins.env"
DEST="$ROOT/external/pw64-decomp"
shopt -s nullglob
patches=("$ROOT"/patches/*.patch)
phash=$( { [ ${#patches[@]} -gt 0 ] && cat ${patches[@]+"${patches[@]}"}; true; } | shasum -a 256 | cut -c1-12)
want="$DECOMP_REV $phash"
if [ "${1:-}" != --force ] && [ -f "$DEST/.wings64-stamp" ] && [ "$(cat "$DEST/.wings64-stamp")" = "$want" ]; then
    exit 0
fi
rm -rf "$DEST"
"$ROOT/docker/fetch-src.sh" "$DECOMP_URL" "$DECOMP_REV" "$DEST"
for p in ${patches[@]+"${patches[@]}"}; do
    echo "fetch_decomp: applying $(basename "$p")"
    patch -d "$DEST" -p1 --forward --silent < "$p"
done
echo "$want" > "$DEST/.wings64-stamp"
echo "fetch_decomp: external/pw64-decomp at ${DECOMP_REV:0:7} (+${#patches[@]} patches)"
