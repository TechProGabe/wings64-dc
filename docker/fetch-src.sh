#!/bin/sh
# fetch-src.sh URL REV DEST — shallow-fetch one commit by SHA, then drop .git.
set -eu
url=$1; rev=$2; dest=$3
mkdir -p "$dest"
cd "$dest"
git init -q
git remote add origin "$url"
git fetch -q --depth 1 origin "$rev"
git checkout -q FETCH_HEAD
test "$(git rev-parse HEAD)" = "$rev" || { echo "fetch-src: $url resolved to $(git rev-parse HEAD), want $rev" >&2; exit 1; }
rm -rf .git
