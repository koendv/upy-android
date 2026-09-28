#!/usr/bin/env bash
# Regenerates upy-android/app/src/main/res/font/upy_symbols.ttf,
# a static-instance, subsetted Material Symbols Rounded font holding only
# the icons this app actually uses.
# Run this again only when the icon set itself changes
# (new_icons.html gets a new icon added or removed) or the pinned
# commit needs bumping.
#
# Requires: curl, python3 with fonttools installed
# (`pip install fonttools`). pyftsubset and fonttools.varLib.instancer
# are both part of that one package.
#
# Usage: ./tools/generate-icon-font.sh

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# google/material-design-icons
MDI_SHA=bd8cb85bd4bad964fe6918f79665bb40c3a8efef
MDI_RAW_BASE="https://raw.githubusercontent.com/google/material-design-icons/$MDI_SHA/variablefont"

# Every icon name this app uses, anywhere.
ICON_NAMES=(
    terminal folder camera settings
    link link_off stop play_arrow restart_alt delete_sweep
    keyboard_arrow_up keyboard_arrow_down
    drive_folder_upload note_add create_new_folder refresh arrow_back
    memory terminal_2 password_2 public lock shop adb
)

OUT_FONT="upy-android/app/src/main/res/font/upy_symbols.ttf"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "- fetching pinned Material Symbols Rounded variable font + codepoints ($MDI_SHA)"
curl -sL "$MDI_RAW_BASE/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.ttf" \
    -o "$WORK_DIR/variable.ttf"
curl -sL "$MDI_RAW_BASE/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.codepoints" \
    -o "$WORK_DIR/rounded.codepoints"

echo "- resolving ${#ICON_NAMES[@]} icon name(s) to codepoints"
UNICODES=""
for name in "${ICON_NAMES[@]}"; do
    cp=$(awk -v n="$name" '$1 == n { print $2; found=1 } END { if (!found) exit 1 }' "$WORK_DIR/rounded.codepoints") \
        || { echo "error: icon name '$name' not found in the pinned codepoints file" >&2; exit 1; }
    UNICODES="${UNICODES}U+${cp},"
done

echo "- pinning variable axes: FILL=0, wght=400, GRAD=0, opsz=28"
python3 -m fontTools.varLib.instancer \
    "$WORK_DIR/variable.ttf" FILL=0 wght=400 GRAD=0 opsz=28 \
    -o "$WORK_DIR/instanced.ttf" --quiet

mkdir -p "$(dirname "$OUT_FONT")"

echo "- subsetting to only the resolved codepoints"
python3 -m fontTools.subset \
    "$WORK_DIR/instanced.ttf" \
    "--unicodes=${UNICODES%,}" \
    --layout-features='' \
    --no-hinting \
    --output-file="$OUT_FONT"

echo "- done: $OUT_FONT ($(du -h "$OUT_FONT" | cut -f1))"
