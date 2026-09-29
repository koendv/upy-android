#!/bin/sh
# Fetches this project's first prebuilt binary native dependency:
# LiteRT (TensorFlow Lite's successor, Apache 2.0).
# See session-state: fetch-litert.sh#LITERT_VERSION
set -e
cd "$(dirname "$0")/.."

LITERT_VERSION=$(sed -n "s/^litert.version=//p" upstream.properties)
AAR_URL="https://dl.google.com/android/maven2/com/google/ai/edge/litert/litert/${LITERT_VERSION}/litert-${LITERT_VERSION}.aar"
OUT_DIR=app/src/main/cpp/litert
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/lib" "$OUT_DIR/include"

echo "fetch-litert: downloading litert-${LITERT_VERSION}.aar..."
curl -fsSL -o "$WORK_DIR/litert.aar" "$AAR_URL"

echo "fetch-litert: extracting arm64-v8a .so files (this project is arm64-v8a only, see build.gradle.kts abiFilters)..."
unzip -oq "$WORK_DIR/litert.aar" "jni/arm64-v8a/libLiteRt.so" "jni/arm64-v8a/libLiteRtClGlAccelerator.so" -d "$WORK_DIR/aar"
cp "$WORK_DIR/aar/jni/arm64-v8a/libLiteRt.so" "$OUT_DIR/lib/libLiteRt.so"
# GPU/OpenCL-GL delegate for litert-api's LiteRtEnvironment. Never
# linked by CMakeLists.txt. libLiteRt.so's own runtime dlopen()s it by
# name, see session-state: fetch-litert.sh#LITERT_VERSION
cp "$WORK_DIR/aar/jni/arm64-v8a/libLiteRtClGlAccelerator.so" "$OUT_DIR/lib/libLiteRtClGlAccelerator.so"

echo "fetch-litert: sparse-cloning LiteRT source at v${LITERT_VERSION} for C API headers only..."
git clone --quiet --depth 1 --branch "v${LITERT_VERSION}" \
    --filter=blob:none --sparse \
    https://github.com/google-ai-edge/LiteRT "$WORK_DIR/LiteRT"
# tflite/c/c_api.h's own transitive #include closure (walked by hand,
# not guessed: tflite/c/{c_api,c_api_experimental,c_api_types,common,
# builtin_op_data}.h chase into these 5 directories/files exactly).
# Confirmed zero diff between this tag and a later HEAD for all of
# these before relying on this, so no version-skew risk.
# --no-cone plus explicit patterns: cone mode (sparse-checkout set's
# default) refuses to mix a single top-level file (tflite/builtin_ops.h)
# with directory patterns in the same call ("not a directory" error,
# confirmed hitting this directly). --no-cone's plain gitignore-style
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

# LiteRtEnvironment/LiteRtCompiledModel, the newer API, originally used
# directly by rt_module.cpp (since deleted). internal/ is fetched as
# part of the same tree, same "whole directory, not hand-picked" style
# as tflite/c/ above. Most of internal/'s own headers were never used
# even when rt_module.cpp existed, and just sit unreferenced.
mkdir -p "$OUT_DIR/include/litert"
cp -r "$WORK_DIR/LiteRT/litert/c" "$OUT_DIR/include/litert/c"

# litert/build_common/build_config.h is not a checked-in header
# upstream, but a Bazel genrule output selecting one of four config
# variants per build target. The default variant
# (config/build_config_gpu_npu.h, "//conditions:default") is genuinely
# empty; this stand-in matches that default exactly. Confirmed by
# reading litert/build_common/BUILD and every config/build_config_*.h
# variant directly, not assumed. Re-check that BUILD file's
# build_config_header rule before trusting this stand-in across a
# future LiteRT version bump.
mkdir -p "$OUT_DIR/include/litert/build_common"
cat > "$OUT_DIR/include/litert/build_common/build_config.h" <<'BUILD_CONFIG_EOF'
#ifndef LITERT_BUILD_COMMON_BUILD_CONFIG_H_
#define LITERT_BUILD_COMMON_BUILD_CONFIG_H_
#endif  // LITERT_BUILD_COMMON_BUILD_CONFIG_H_
BUILD_CONFIG_EOF

# libLiteRtClGlAccelerator.so is never a CMake link target (dlopen()'d
# by name only, see session-state: fetch-litert.sh#LITERT_VERSION) and,
# unlike libLiteRt.so, isn't auto-packaged by AGP's CMake-IMPORTED-
# target mechanism either. libLiteRtClGlAccelerator.so must be staged
# where AGP scans for prebuilt .so's by convention (jniLibs/<abi>/) to
# ship in the APK at all. Used to be a manual one-off copy step, folded
# in here so a fresh clone building for the first time gets it automatically.
mkdir -p app/src/main/jniLibs/arm64-v8a
cp "$OUT_DIR/lib/libLiteRtClGlAccelerator.so" app/src/main/jniLibs/arm64-v8a/

echo "fetch-litert: done -- $(du -h "$OUT_DIR/lib/libLiteRt.so" | cut -f1) libLiteRt.so, $(du -h "$OUT_DIR/lib/libLiteRtClGlAccelerator.so" | cut -f1) libLiteRtClGlAccelerator.so, $(find "$OUT_DIR/include" -name '*.h' | wc -l) headers"
