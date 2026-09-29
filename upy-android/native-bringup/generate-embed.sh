#!/bin/sh
# Regenerates micropython_embed/ from ../upstream/micropython plus vendor/
# and my-overrides/, then syncs it into ../app/src/main/cpp/micropython_embed.
# The sync only rewrites changed files, so ninja only recompiles those.
set -e
cd "$(dirname "$0")"

rm -rf build-embed micropython_embed
make -f micropython_embed.mk MICROPYTHON_TOP="$(cd ../upstream/micropython && pwd)"
./apply-overrides.sh
python3 sync-tree.py micropython_embed ../app/src/main/cpp/micropython_embed
