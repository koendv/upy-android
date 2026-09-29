#!/bin/sh
# Manual, once per LiteRT upgrade: refreshes the LiteRT C API headers
# committed in ../app/src/main/cpp/litert/include/ from the LiteRT source
# tag matching litert.version in ../upstream.properties.
# Review the diff and commit it. See ../app/src/main/cpp/litert/README.md.
set -e
cd "$(dirname "$0")/.."

LITERT_VERSION=$(sed -n "s/^litert.version=//p" upstream.properties)
OUT_DIR=app/src/main/cpp/litert
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

rm -rf "$OUT_DIR/include"
mkdir -p "$OUT_DIR/include"

echo "update-litert-headers: sparse-cloning LiteRT source at v${LITERT_VERSION} for C API headers only..."
git clone --quiet --depth 1 --branch "v${LITERT_VERSION}" \
    --filter=blob:none --sparse \
    https://github.com/google-ai-edge/LiteRT "$WORK_DIR/LiteRT"
# Only litert/c/*.h: litert_module.cpp includes litert/c/{litert_common,
# litert_environment,litert_model,litert_model_types}.h, whose include
# closure stays inside litert/c/ plus build_config.h below. The whole
# directory, not just those files, so a new include between litert/c
# headers in a later release still resolves. tflite/ and litert/c/internal/
# are not used by this project.
git -C "$WORK_DIR/LiteRT" sparse-checkout set --quiet --no-cone '/litert/c/*.h'

mkdir -p "$OUT_DIR/include/litert/c"
cp "$WORK_DIR"/LiteRT/litert/c/*.h "$OUT_DIR/include/litert/c/"

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

# Checked by app/build.gradle.kts against litert.version.
echo "$LITERT_VERSION" > "$OUT_DIR/include/LITERT_VERSION"

echo "update-litert-headers: $(find "$OUT_DIR/include" -name '*.h' | wc -l) headers for LiteRT $LITERT_VERSION"
