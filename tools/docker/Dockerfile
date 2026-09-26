# syntax=docker/dockerfile:1

# Build upy-android's upy-debug.apk (or a simple debug-key-signed
# upy-release.apk) inside Docker. MicroPython running as an Android app.
#
# Only upy-android/'s own sources are embedded in this repo (COPY'd in,
# see README). Every genuine third-party dependency (MicroPython,
# OpenMV's imlib, ulab) is fetched fresh from upstream at a pinned commit
# instead of being vendored into this repo's own git history.
#
# see session-state: Dockerfile#openmv_vendoring_audit
#
# Regenerated via upy-android/native-bringup/micropython_embed.mk +
# upy-android/native-bringup/apply-overrides.sh, the same two-step
# process the real project's own dev workflow uses.
# app/src/main/cpp/micropython_embed/ is NOT embedded in this repo either
# (it's entirely reconstructable from the steps above); it's generated
# fresh at build time.
#
# Used in:
# - github workflows
# - local builds
#
# Two-phase locally: build "tools" once (rebuild only when a pinned SDK/
# NDK/cmake version above changes), then "build" every time after --
# BuildKit's own layer cache already makes an unchanged "tools" stage a
# no-op, this split just documents/names that so it's not accidental.
# The "build" stage's own compile step is BuildKit-cache-mounted
# (Gradle's cache dir + app/.cxx + app/build), so a rebuild after a small
# source change only recompiles/relinks what actually changed instead of
# a full cold build -- requires a BuildKit-enabled `docker build` (the
# default on any reasonably recent Docker).
#
# Local debug build:
#   docker build --target tools -t upy-android-tools .
#   docker build --target build -t upy-android .
#   id=$(docker create upy-android)
#   docker cp "$id":/output ./upy-android_apk
#   docker rm "$id"
#
# Local release build (debug-key-signed so it's installable, NOT a
# production signing identity, see app/build.gradle.kts):
#   docker build --target build --build-arg BUILD_TYPE=release -t upy-android .
#   (then the same docker create/cp/rm steps)
#
# Tested:
# - amd64/ubuntu
#

FROM ubuntu:24.04 AS tools

# Pinned upstream commits. Every genuine third-party dependency, none
# vendored into this repo's own git history (see header comment above).
ENV MICROPYTHON_SHA=52b5fbcb4b7961356c83ba0c533ee42acdfa0169
ENV OPENMV_SHA=818f111d49effc98c0c6361f8fe85cb68deac9eb
ENV ULAB_SHA=01ad8a5fff89d7aeb68ef1827d1ba8f83f6462a7
# TFLM (TensorFlow Lite Micro) -- the real interpreter + reference-
# kernel backend behind `import ml`/`import tf` (Part 5, see
# SESSION_STATE.yaml). Exact commit tflm_backend.cc (my-overrides/
# openmv/, patched) was written/tested against, per openmv/tflm's own
# CI workflow (.github/workflows/tflm.yml).
ENV TFLM_SHA=a57349f0bc3bc3ded105238fe2f14c36888d42b0

ARG DEST_DIR=/root
WORKDIR ${DEST_DIR}

RUN apt-get update

RUN apt-get install -y apt-utils

# python3-numpy/python3-pil: NOT needed for anything this project
# actually uses -- required only because TFLM's own Makefile
# unconditionally includes every examples/*/Makefile.inc fragment at
# PARSE time (Part 5's own vendor/tflm/ fetch below, `make ...
# third_party_downloads`), and person_detection's own fragment shells
# out to generate_cc_arrays.py (numpy + PIL) as a side effect of being
# parsed at all, regardless of which target was actually requested.
# Confirmed via a real Docker build failing twice (ModuleNotFoundError:
# numpy, then PIL) before finding this, not guessed ahead of time --
# see SESSION_STATE.yaml's Part 5 entry.
# openssl: TFLM's own download_and_extract.sh (same fetch step) uses
# `openssl dgst -md5` to verify each downloaded archive.
RUN apt-get install -y \
      git wget unzip curl ca-certificates openjdk-21-jdk-headless \
      build-essential python3 python3-numpy python3-pil openssl

# build runs as non-root user
ARG USER_UID=1000
ARG USER_GID=1000
RUN if [ "$USER_UID" != "1000" ] || [ "$USER_GID" != "1000" ]; then \
        groupadd -g $USER_GID devuser \
        && useradd -u $USER_UID -g $USER_GID -m -s /bin/bash devuser; \
    else \
        groupmod -n devuser ubuntu \
        && usermod -l devuser -d /home/devuser -m ubuntu; \
    fi

# /output directory for build results
RUN mkdir -p /output && chown $USER_UID:$USER_GID /output

USER devuser
WORKDIR /home/devuser

ENV JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64

# Android SDK cmdline-tools, then the exact platform/build-tools/NDK/CMake
# versions app/build.gradle.kts pins (compileSdk=37, ndkVersion=
# 30.0.16248370). All version numbers below read directly off a real,
# already-working local install (sdkmanager --list_installed), not
# guessed. The cmdline-tools download build number is the one part of
# this file most likely to need bumping over time; check
# https://developer.android.com/studio#command-line-tools-only for a
# current one if this step ever fails to fetch.
ENV ANDROID_SDK_ROOT=/home/devuser/android-sdk
ENV ANDROID_CMDLINE_TOOLS_BUILD=11076708
RUN mkdir -p ${ANDROID_SDK_ROOT}/cmdline-tools \
    && cd ${ANDROID_SDK_ROOT}/cmdline-tools \
    && wget -q "https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_CMDLINE_TOOLS_BUILD}_latest.zip" -O cmdline-tools.zip \
    && unzip -q cmdline-tools.zip \
    && rm cmdline-tools.zip \
    && mv cmdline-tools latest

ENV PATH=${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${PATH}

RUN yes | sdkmanager --licenses >/dev/null 2>&1; \
    sdkmanager --install \
         "platform-tools" \
         "platforms;android-37.0" \
         "build-tools;37.0.0" \
         "ndk;30.0.16248370" \
         "cmake;3.22.1"

# ---------------------------------------------------------------------------
# "build" stage: takes tools and sources, builds the APK.

FROM tools AS build

ARG BUILD_TYPE=debug

RUN git clone https://github.com/micropython/micropython.git /home/devuser/micropython \
    && git -C /home/devuser/micropython checkout $MICROPYTHON_SHA

RUN git clone https://github.com/openmv/openmv.git /home/devuser/openmv \
    && git -C /home/devuser/openmv checkout $OPENMV_SHA

RUN git clone https://github.com/v923z/micropython-ulab.git /home/devuser/ulab \
    && git -C /home/devuser/ulab checkout $ULAB_SHA

RUN git clone https://github.com/tensorflow/tflite-micro.git /home/devuser/tflite-micro \
    && git -C /home/devuser/tflite-micro checkout $TFLM_SHA

# upy-android's own sources, embedded in this repo, not cloned (see
# header comment). This includes my-overrides/openmv/'s 14 files with no
# fetchable upstream equivalent and my-overrides/ulab/'s one (ulab_
# config.h) -- native-bringup/vendor/{openmv,ulab}/ is gitignored (see
# .gitignore) and not part of this COPY at all; populated from the
# clones above, next.
COPY --chown=devuser:devuser upy-android /home/devuser/upy-android
COPY --chown=devuser:devuser tools/docker/openmv-manifest.tsv /home/devuser/openmv-manifest.tsv

WORKDIR /home/devuser/upy-android/native-bringup

# Populate vendor/openmv/ with the 68 confirmed-pristine files from the
# pinned OpenMV clone, per-file (not a directory copy). The manifest maps
# this flat directory's filenames back to their real, differently
# structured upstream paths, e.g. lib/imlib/blob.c -> blob.c.
RUN mkdir -p vendor/openmv \
    && while IFS=$(printf '\t') read -r src dest; do \
        cp "/home/devuser/openmv/$src" "vendor/openmv/$dest"; \
    done < /home/devuser/openmv-manifest.tsv

# LiteRT (android.tf module): this project's first PREBUILT BINARY
# dependency, fetched fresh the same way the real dev workflow does (see
# fetch-litert.sh's own header comment): the AAR's .so from
# dl.google.com plus a sparse-cloned header set from the matching
# upstream tag. Must run before the qstr-scan step below.
# tf_module.cpp's #include "tflite/c/c_api.h" needs
# app/src/main/cpp/litert/include to already exist.
RUN ./fetch-litert.sh

# Populate vendor/ulab/ wholesale. Confirmed byte-identical to the
# pinned ulab clone's code/ directory, minus two build-integration files
# (micropython.cmake/micropython.mk, its generic "user C module" build
# integration) this project's own hand-rolled CMakeLists.txt doesn't use.
RUN mkdir -p vendor/ulab \
    && cp /home/devuser/ulab/code/*.c /home/devuser/ulab/code/*.h vendor/ulab/ \
    && find /home/devuser/ulab/code -mindepth 1 -maxdepth 1 -type d -exec cp -r {} vendor/ulab/ \;

# Populate vendor/tflm/ via TFLM's own real project-generation script
# (create_tflm_tree.py) -- NOT hand-picked files, matches the exact
# file set tflm_backend.cc needs. No --makefile_options override at
# all: TARGET defaults to $(HOST_OS) (confirmed by reading the real
# Makefile -- TARGET := $(HOST_OS), and TARGETS_WITHOUT_MAKEFILES :=
# $(HOST_OS) means this deliberately skips any bare-metal/Cortex-M-
# specific target fragment), and OPTIMIZED_KERNEL_DIR stays unset,
# defaulting to portable reference kernels rather than cmsis_nn/
# ethos_u -- exactly what this project's own CMake build compiles
# directly (bypassing TFLM's own Makefile/toolchain machinery
# entirely), same "vendor + compile under our own build" pattern as
# ulab/imlib above. third_party_downloads fetches flatbuffers/
# gemmlowp/ruy/kissfft from their own upstream sources -- needs network
# access, same tier as the git clones above.
RUN cd /home/devuser/tflite-micro \
    && make -f tensorflow/lite/micro/tools/make/Makefile third_party_downloads \
    && rm -rf /home/devuser/upy-android/native-bringup/vendor/tflm \
    && python3 tensorflow/lite/micro/tools/project_generation/create_tflm_tree.py \
           /home/devuser/upy-android/native-bringup/vendor/tflm

# Swap in the ONE debug-log implementation that actually routes
# through RegisterDebugLogCallback -- tflm_backend.cc's own #include
# is hardcoded to this exact "cortex_m_generic/" path, and the
# default/host file list above pulls in the plain tensorflow/lite/
# micro/debug_log.cc instead (hardcodes vfprintf(stderr), which goes
# nowhere on Android, and doesn't even define RegisterDebugLogCallback
# at all -- confirmed by diffing the two files directly, not guessed;
# see SESSION_STATE.yaml's Part 5 entry). Both files are genuinely
# portable despite living under a "cortex_m_generic" directory name.
# array.cc is deleted (array.h stays, header-only) -- it cannot
# compile under TF_LITE_STATIC_MEMORY (its own deleter calls
# TfLiteIntArrayFree/TfLiteFloatArrayFree, both declared only when
# that macro is NOT defined), and nothing in this build's own
# TF_LITE_STATIC_MEMORY configuration ever instantiates the template
# functions that would otherwise need it (confirmed via grep against
# kernel_util.cc's own matching #ifndef TF_LITE_STATIC_MEMORY guard).
RUN cd /home/devuser/upy-android/native-bringup/vendor/tflm \
    && rm -f tensorflow/lite/micro/debug_log.cc \
    && mkdir -p tensorflow/lite/micro/cortex_m_generic \
    && cp /home/devuser/tflite-micro/tensorflow/lite/micro/cortex_m_generic/debug_log.cc \
          tensorflow/lite/micro/cortex_m_generic/debug_log.cc \
    && cp /home/devuser/tflite-micro/tensorflow/lite/micro/cortex_m_generic/debug_log_callback.h \
          tensorflow/lite/micro/cortex_m_generic/debug_log_callback.h \
    && rm -f tensorflow/lite/array.cc

# Regenerate the MicroPython embed-port tree fresh from the pinned
# upstream clone, then reapply our own override/new files on top. Same
# two-step process the real project's own dev workflow uses.
# MICROPYTHON_TOP is overridden on the command line (a plain `=`
# assignment in micropython_embed.mk, not `override`, so this wins). No
# source file needs patching for this container's own paths.
RUN rm -rf build-embed micropython_embed \
    && make -f micropython_embed.mk MICROPYTHON_TOP=/home/devuser/micropython \
    && ./apply-overrides.sh

# Sync the freshly-regenerated tree into the app's own source tree,
# replacing whatever came along in the COPY above. This is what makes
# the build genuinely reproducible from upstream + our own patches,
# rather than silently depending on a pre-generated snapshot.
RUN rm -rf ../app/src/main/cpp/micropython_embed \
    && cp -r micropython_embed ../app/src/main/cpp/micropython_embed

# Same sync as above, for vendor/tflm/ -- no merge step needed (unlike
# micropython_embed/, tflm/ has no my-overrides/ counterpart to fold
# in; it's 100% pristine + the one debug-log swap already applied above).
RUN rm -rf ../app/src/main/cpp/tflm \
    && cp -r vendor/tflm ../app/src/main/cpp/tflm

WORKDIR /home/devuser/upy-android

RUN echo "sdk.dir=${ANDROID_SDK_ROOT}" > local.properties

RUN chmod +x ./gradlew

# ONE-TIME: generate app/gradle.lockfile to pin down dependencies
#RUN ./gradlew :app:dependencies --write-locks && \
#    find . -name 'gradle.lockfile' -o -name '*gradle.lockfile*' && \
#    cp app/gradle.lockfile /output/gradle.lockfile

# Cache-mounted: Gradle's dependency/build cache plus CMake/Ninja's own
# incremental object-file state (app/.cxx) and Gradle's task-output
# state (app/build) all survive across separate `docker build`
# invocations, so an unchanged source tree recompiles nothing and a
# small change only rebuilds what depends on it. A cache mount's
# contents are NOT part of the image filesystem once this RUN ends --
# the APK copy below must happen in this same RUN, not a later one, or
# it would see an empty app/build.
RUN --mount=type=cache,target=/home/devuser/.gradle,uid=1000,gid=1000 \
    --mount=type=cache,target=/home/devuser/upy-android/app/.cxx,uid=1000,gid=1000 \
    --mount=type=cache,target=/home/devuser/upy-android/app/build,uid=1000,gid=1000 \
    if [ "$BUILD_TYPE" = "release" ]; then \
        ./gradlew :app:assembleRelease && \
        cp app/build/outputs/apk/release/app-release.apk /output/upy-release.apk; \
    else \
        ./gradlew :app:assembleDebug && \
        cp app/build/outputs/apk/debug/app-debug.apk /output/upy-debug.apk; \
    fi

# not truncated
