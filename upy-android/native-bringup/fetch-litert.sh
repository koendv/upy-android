#!/bin/sh
# Fetches this project's first PREBUILT BINARY native dependency: LiteRT
# (TensorFlow Lite's successor, Apache 2.0), for the android.tf module (see
# SESSION_STATE.yaml's "android.tf" design discussion).
#
# Unlike everything else in native-bringup/ (micropython/openmv/ulab, all
# compiled from vendored SOURCE), there is no "build from source" step here
# -- LiteRT's C API .so is only distributed prebuilt. The two pieces come
# from two different places, both pinned to the same litert release
# (2.2.0, matching the Maven coordinate in app/build.gradle.kts):
#
# 1. The .so itself: extracted directly from Google's own published AAR
#    (dl.google.com, same artifact Gradle resolves at
#    com.google.ai.edge.litert:litert:2.2.0) -- NOT from Gradle's own
#    module cache (unstable hash-named subdirectory, and not guaranteed
#    downloaded yet at CMake-configure time; only :app:assembleDebug
#    actually pulls the binary in, :app:dependencies only resolves POM
#    metadata -- confirmed directly, not assumed).
# 2. The C headers: the AAR itself ships NONE (confirmed by extracting it
#    directly and finding zero .h files) -- pulled from a shallow, sparse
#    clone of the upstream LiteRT source at the matching v2.2.0 tag
#    instead. Confirmed zero diff between v2.2.0 and a later HEAD for
#    these exact files before relying on this, so no version-skew risk.
#
# Output: app/src/main/cpp/litert/{lib/libLiteRt.so,include/tflite/...} --
# gitignored, same ephemeral treatment as micropython_embed/ (regenerate
# via this script, don't check in the binary or a personal clone path).
set -e
cd "$(dirname "$0")/.."

LITERT_VERSION=2.2.0
AAR_URL="https://dl.google.com/android/maven2/com/google/ai/edge/litert/litert/${LITERT_VERSION}/litert-${LITERT_VERSION}.aar"
OUT_DIR=app/src/main/cpp/litert
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/lib" "$OUT_DIR/include"

echo "fetch-litert: downloading litert-${LITERT_VERSION}.aar..."
curl -sL -o "$WORK_DIR/litert.aar" "$AAR_URL"

echo "fetch-litert: extracting arm64-v8a .so (this project is arm64-v8a only, see build.gradle.kts abiFilters)..."
unzip -oq "$WORK_DIR/litert.aar" "jni/arm64-v8a/libLiteRt.so" -d "$WORK_DIR/aar"
cp "$WORK_DIR/aar/jni/arm64-v8a/libLiteRt.so" "$OUT_DIR/lib/libLiteRt.so"

echo "fetch-litert: sparse-cloning LiteRT source at v${LITERT_VERSION} for C API headers only..."
git clone --quiet --depth 1 --branch "v${LITERT_VERSION}" \
    --filter=blob:none --sparse \
    https://github.com/google-ai-edge/LiteRT "$WORK_DIR/LiteRT"
# tflite/c/c_api.h's OWN transitive #include closure (walked by hand,
# not guessed -- tflite/c/{c_api,c_api_experimental,c_api_types,common,
# builtin_op_data}.h chase into these 5 directories/files exactly; see
# SESSION_STATE.yaml). Confirmed zero diff between this tag and a later
# HEAD for all of these before relying on this, so no version-skew risk.
# --no-cone + explicit patterns: cone mode (sparse-checkout set's
# default) refuses to mix a single top-level file (tflite/builtin_ops.h)
# with directory patterns in the same call ("not a directory" error,
# confirmed hitting this directly) -- --no-cone's plain gitignore-style
# patterns handle both in one call.
git -C "$WORK_DIR/LiteRT" sparse-checkout set --quiet --no-cone \
    '/tflite/*.h' \
    'tflite/c/' \
    'tflite/core/c/' \
    'tflite/core/async/c/' \
    'tflite/converter/core/c/'

mkdir -p "$OUT_DIR/include/tflite"
cp "$WORK_DIR/LiteRT/tflite/builtin_ops.h" "$OUT_DIR/include/tflite/"
cp -r "$WORK_DIR/LiteRT/tflite/c" "$OUT_DIR/include/tflite/c"
mkdir -p "$OUT_DIR/include/tflite/core"
cp -r "$WORK_DIR/LiteRT/tflite/core/c" "$OUT_DIR/include/tflite/core/c"
cp -r "$WORK_DIR/LiteRT/tflite/core/async" "$OUT_DIR/include/tflite/core/async"
mkdir -p "$OUT_DIR/include/tflite/converter"
cp -r "$WORK_DIR/LiteRT/tflite/converter/core" "$OUT_DIR/include/tflite/converter/core"

echo "fetch-litert: done -- $(du -h "$OUT_DIR/lib/libLiteRt.so" | cut -f1) .so, $(find "$OUT_DIR/include" -name '*.h' | wc -l) headers"
