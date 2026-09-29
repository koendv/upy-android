#!/bin/sh
# Fetches micropython, openmv, apriltag and ulab at the commits pinned in
# ../upstream.properties into ../upstream/<name>. Shallow, one commit each.
# No-op for a checkout already at its pinned commit.
set -e
cd "$(dirname "$0")/.."

prop() { sed -n "s/^$1=//p" upstream.properties; }

for name in micropython openmv apriltag ulab; do
    url=$(prop "$name.url")
    sha=$(prop "$name.sha")
    dir=upstream/$name
    if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" = "$sha" ]; then
        continue
    fi
    echo "fetch-upstream: $name $sha"
    rm -rf "$dir"
    git init -q "$dir"
    git -C "$dir" fetch -q --depth 1 "$url" "$sha"
    git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
done
