#!/bin/sh
# Generates the Haar cascades image.HaarCascade() loads from /rom/,
# using OpenMV's own converter on OpenMV's own XML (../upstream/openmv).
# Output: vendor/rom/, copied into app assets by copyRom (build.gradle.kts).
set -e
cd "$(dirname "$0")"

OPENMV=../upstream/openmv
OUT_DIR=vendor/rom

mkdir -p "$OUT_DIR"
# name stages: same stage counts OpenMV's own examples pass to HaarCascade().
for spec in "frontalface 25" "eye 24"; do
    set -- $spec
    python3 "$OPENMV/tools/haar2c.py" -s "$2" -n "$OUT_DIR/haarcascade_$1" \
        "$OPENMV/lib/haar/haarcascade_$1.xml" > /dev/null
    echo "gen-cascades: $OUT_DIR/haarcascade_$1.cascade ($2 stages)"
done
