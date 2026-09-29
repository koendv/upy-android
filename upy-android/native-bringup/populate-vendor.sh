#!/bin/sh
# Fills vendor/ (gitignored) from ../upstream/: vendor/openmv/ per
# openmv-manifest.tsv (upstream path -> flat name), vendor/ulab/ from
# ulab's code/ directory minus its micropython.cmake/.mk build glue.
set -e
cd "$(dirname "$0")"

UPSTREAM=../upstream

rm -rf vendor/openmv vendor/ulab
mkdir -p vendor/openmv vendor/ulab

while IFS=$(printf '\t') read -r src dest; do
    cp "$UPSTREAM/openmv/$src" "vendor/openmv/$dest"
done < openmv-manifest.tsv

cp "$UPSTREAM"/ulab/code/*.c "$UPSTREAM"/ulab/code/*.h vendor/ulab/
find "$UPSTREAM/ulab/code" -mindepth 1 -maxdepth 1 -type d -exec cp -r {} vendor/ulab/ \;
