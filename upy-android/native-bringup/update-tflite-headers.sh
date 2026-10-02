#!/bin/sh
# Manual, once per LiteRT upgrade: refreshes the classic TensorFlow Lite C
# API headers committed in ../app/src/main/cpp/tflite/include/, from the
# SAME LiteRT source tag as update-litert-headers.sh (litert.version in
# ../upstream.properties). Review the diff and commit it. See
# ../app/src/main/cpp/tflite/README.md.
#
# Why from the LiteRT repo, not upstream TensorFlow: tflite_module.cpp
# links the same libLiteRt.so litert_module.cpp already links (one
# process, one copy of TFLite -- see session-state for the symbol-
# collision reasoning). libLiteRt.so is built from LiteRT's OWN copy of
# the TFLite source (tag v<litert.version>), which has since renamed the
# public include path from tensorflow/lite/... to tflite/.... Headers
# from a different TensorFlow Lite release would not reliably match the
# actual binary's ABI.
set -e
cd "$(dirname "$0")/.."

LITERT_VERSION=$(sed -n "s/^litert.version=//p" upstream.properties)
OUT_DIR=app/src/main/cpp/tflite
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

rm -rf "$OUT_DIR/include"
mkdir -p "$OUT_DIR/include"

echo "update-tflite-headers: sparse-cloning LiteRT source at v${LITERT_VERSION} for TFLite C API headers only..."
git clone --quiet --depth 1 --branch "v${LITERT_VERSION}" \
    --filter=blob:none --sparse \
    https://github.com/google-ai-edge/LiteRT "$WORK_DIR/LiteRT"
# This set is the real #include closure of tflite/c/c_api.h, confirmed by
# preprocessing it (clang -E -M), not guessed from directory listings --
# see session-state. Re-run that check on every version bump: a future
# release could add a new transitive header.
git -C "$WORK_DIR/LiteRT" sparse-checkout set --quiet --no-cone \
    '/tflite/c/c_api.h' \
    '/tflite/builtin_ops.h' \
    '/tflite/core/async/c/types.h' \
    '/tflite/core/c/c_api.h' \
    '/tflite/core/c/c_api_types.h' \
    '/tflite/core/c/operator.h' \
    '/tflite/converter/core/c/tflite_types.h'

for f in tflite/c/c_api.h tflite/builtin_ops.h tflite/core/async/c/types.h \
    tflite/core/c/c_api.h tflite/core/c/c_api_types.h tflite/core/c/operator.h \
    tflite/converter/core/c/tflite_types.h; do
    mkdir -p "$OUT_DIR/include/$(dirname "$f")"
    cp "$WORK_DIR/LiteRT/$f" "$OUT_DIR/include/$f"
done

# Checked by app/build.gradle.kts against litert.version, same convention
# as litert/include/LITERT_VERSION.
echo "$LITERT_VERSION" > "$OUT_DIR/include/LITERT_VERSION"

echo "update-tflite-headers: $(find "$OUT_DIR/include" -name '*.h' | wc -l) headers for LiteRT $LITERT_VERSION"
