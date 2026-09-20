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
# 1. The .so files: extracted directly from Google's own published AAR
#    (dl.google.com, same artifact Gradle resolves at
#    com.google.ai.edge.litert:litert:2.2.0) -- NOT from Gradle's own
#    module cache (unstable hash-named subdirectory, and not guaranteed
#    downloaded yet at CMake-configure time; only :app:assembleDebug
#    actually pulls the binary in, :app:dependencies only resolves POM
#    metadata -- confirmed directly, not assumed). Two .so's ship here:
#    libLiteRt.so (the classic TfLiteInterpreter C API, used by
#    android.tf, linked by CMakeLists.txt) and libLiteRtClGlAccelerator.so
#    (a GPU/OpenCL-GL delegate, used by android.rt's LiteRtEnvironment).
#    The GPU delegate is never linked directly -- libLiteRt.so's own
#    runtime dlopen()s it by name at LiteRtCreateEnvironment() time --
#    it only needs to be present in the app's own native library dir,
#    which is Gradle's jniLibs packaging, not CMakeLists.txt.
# 2. The C headers: the AAR itself ships NONE (confirmed by extracting it
#    directly and finding zero .h files) -- pulled from a shallow, sparse
#    clone of the upstream LiteRT source at the matching v2.2.0 tag
#    instead. Confirmed zero diff between v2.2.0 and a later HEAD for
#    these exact files before relying on this, so no version-skew risk.
#    Two API surfaces are fetched: tflite/c/* (the classic API,
#    android.tf) and litert/c/* (the newer LiteRtEnvironment/
#    LiteRtCompiledModel API, android.rt) -- see SESSION_STATE.yaml for
#    how each header set's own transitive closure was walked and
#    verified against the real .so's exported symbols.
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

echo "fetch-litert: extracting arm64-v8a .so files (this project is arm64-v8a only, see build.gradle.kts abiFilters)..."
unzip -oq "$WORK_DIR/litert.aar" "jni/arm64-v8a/libLiteRt.so" "jni/arm64-v8a/libLiteRtClGlAccelerator.so" -d "$WORK_DIR/aar"
cp "$WORK_DIR/aar/jni/arm64-v8a/libLiteRt.so" "$OUT_DIR/lib/libLiteRt.so"
# GPU/OpenCL-GL delegate for android.rt. Never linked by CMakeLists.txt --
# libLiteRt.so's own runtime dlopen()s it by name, see this file's own
# header comment.
cp "$WORK_DIR/aar/jni/arm64-v8a/libLiteRtClGlAccelerator.so" "$OUT_DIR/lib/libLiteRtClGlAccelerator.so"

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
    'tflite/converter/core/c/' \
    'litert/c/' \
    'litert/c/internal/'

mkdir -p "$OUT_DIR/include/tflite"
cp "$WORK_DIR/LiteRT/tflite/builtin_ops.h" "$OUT_DIR/include/tflite/"
cp -r "$WORK_DIR/LiteRT/tflite/c" "$OUT_DIR/include/tflite/c"
mkdir -p "$OUT_DIR/include/tflite/core"
cp -r "$WORK_DIR/LiteRT/tflite/core/c" "$OUT_DIR/include/tflite/core/c"
cp -r "$WORK_DIR/LiteRT/tflite/core/async" "$OUT_DIR/include/tflite/core/async"
mkdir -p "$OUT_DIR/include/tflite/converter"
cp -r "$WORK_DIR/LiteRT/tflite/converter/core" "$OUT_DIR/include/tflite/converter/core"

# For android.rt (LiteRtEnvironment/LiteRtCompiledModel, the newer API --
# see SESSION_STATE.yaml). internal/ is fetched as part of the same tree,
# same "whole directory, not hand-picked" style as tflite/c/ above -- the
# handful of internal/ headers android.rt doesn't use just sit unreferenced.
mkdir -p "$OUT_DIR/include/litert"
cp -r "$WORK_DIR/LiteRT/litert/c" "$OUT_DIR/include/litert/c"

# litert/build_common/build_config.h is not a checked-in header upstream --
# it is a Bazel genrule output selecting one of four config variants per
# build target. The default variant (config/build_config_gpu_npu.h,
# "//conditions:default") is genuinely empty; this stand-in matches that
# default exactly. Confirmed this session by reading litert/build_common/
# BUILD and every config/build_config_*.h variant directly, not assumed --
# re-check that BUILD file's build_config_header rule before trusting this
# stand-in across a future LiteRT version bump.
mkdir -p "$OUT_DIR/include/litert/build_common"
cat > "$OUT_DIR/include/litert/build_common/build_config.h" <<'BUILD_CONFIG_EOF'
#ifndef LITERT_BUILD_COMMON_BUILD_CONFIG_H_
#define LITERT_BUILD_COMMON_BUILD_CONFIG_H_
#endif  // LITERT_BUILD_COMMON_BUILD_CONFIG_H_
BUILD_CONFIG_EOF

# libLiteRtClGlAccelerator.so is never a CMake link target (dlopen()'d
# by name only, see this file's own header comment) and, unlike
# libLiteRt.so, isn't auto-packaged by AGP's CMake-IMPORTED-target
# mechanism either -- it must be staged where AGP scans for prebuilt
# .so's by convention (jniLibs/<abi>/) to ship in the APK at all. Used
# to be a manual one-off copy step; folded in here so a fresh clone
# building for the first time gets it automatically.
mkdir -p app/src/main/jniLibs/arm64-v8a
cp "$OUT_DIR/lib/libLiteRtClGlAccelerator.so" app/src/main/jniLibs/arm64-v8a/

echo "fetch-litert: done -- $(du -h "$OUT_DIR/lib/libLiteRt.so" | cut -f1) libLiteRt.so, $(du -h "$OUT_DIR/lib/libLiteRtClGlAccelerator.so" | cut -f1) libLiteRtClGlAccelerator.so, $(find "$OUT_DIR/include" -name '*.h' | wc -l) headers"
