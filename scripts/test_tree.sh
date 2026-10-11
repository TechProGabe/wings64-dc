#!/usr/bin/env bash
# test_tree: run every tools/check/*_check.py from the repo root.
# Continues past failures, prints a summary line, exits 1 if any check failed.
# Bash 3.2 compatible (no associative arrays, no mapfile).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here/.."

manifest="$(
    for f in tools/check/*_check.py; do
        if [ -f "$f" ]; then
            printf '%s\n' "$f"
        fi
    done | LC_ALL=C sort
)"

total=0
failed=0
while IFS= read -r check; do
    if [ -z "$check" ]; then
        continue
    fi
    total=$((total + 1))
    if python3 "$check"; then
        :
    else
        failed=$((failed + 1))
    fi
done <<EOF_LIST
$manifest
EOF_LIST

echo "test_tree: $total checks, $failed failed"
if [ "$failed" -gt 0 ]; then
    exit 1
fi
exit 0
