#!/bin/sh
# Fills vendor/ (gitignored) from ../upstream/: vendor/openmv/ per
# openmv-manifest.tsv (upstream path -> flat name), vendor/ulab/ from
# ulab's code/ directory minus its micropython.cmake/.mk build glue,
# vendor/apriltag/ with the sources apriltag's own apriltag.mk lists plus
# all headers (root and common/).
set -e
cd "$(dirname "$0")"

UPSTREAM=../upstream

rm -rf vendor/openmv vendor/ulab vendor/apriltag
mkdir -p vendor/openmv vendor/ulab vendor/apriltag/common

while IFS=$(printf '\t') read -r src dest; do
    cp "$UPSTREAM/openmv/$src" "vendor/openmv/$dest"
done < openmv-manifest.tsv

cp "$UPSTREAM"/ulab/code/*.c "$UPSTREAM"/ulab/code/*.h vendor/ulab/
find "$UPSTREAM/ulab/code" -mindepth 1 -maxdepth 1 -type d -exec cp -r {} vendor/ulab/ \;

cp "$UPSTREAM"/apriltag/*.h vendor/apriltag/
cp "$UPSTREAM"/apriltag/common/*.h vendor/apriltag/common/
for src in $(sed -n '/^APRILTAG_SRC_C *+=/,/^$/p' "$UPSTREAM/apriltag/apriltag.mk" | grep -o '[A-Za-z0-9_/]*\.c'); do
    cp "$UPSTREAM/apriltag/$src" "vendor/apriltag/$src"
done
# Not in apriltag.mk: image_u8.c references pnm_create_from_file().
# OpenMV's firmware link drops that unused function; this app's link
# (no --gc-sections) needs the definition.
cp "$UPSTREAM/apriltag/common/pnm.c" vendor/apriltag/common/
