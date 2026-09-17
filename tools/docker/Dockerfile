# Build upy-android's upy-debug.apk (or a simple debug-key-signed
# upy-release.apk) inside Docker -- MicroPython running as an Android app.
#
# Only upy-android/'s own sources are embedded in this repo (COPY'd in,
# see README) -- every genuine third-party dependency (MicroPython,
# OpenMV's imlib, ulab) is fetched fresh from upstream at a pinned commit
# instead of being vendored into this repo's own git history.
#
# For OpenMV specifically this required a real per-file audit (not just
# "clone the whole thing"): upy-android/native-bringup/my-overrides/openmv/
# mixes genuinely pristine vendored imlib/py_image files with files that
# only share a filename with a real OpenMV file but are actually written
# from scratch for this Android port (board_config.h, imlib_config.h) or
# drastically trimmed down (omv_csi.h: 29 lines here vs 624 upstream, and
# similar for arm_math.h/cmsis_compiler.h/cmsis_extension.h/framebuffer.h/
# omv_cycles.h/omv_gpu.h), plus files with no upstream equivalent at all
# (link_stubs.c, cmsis_mcu_stub.h, umalloc_android.c, gc.h,
# ulab-shim/ulab/code/ndarray.h). Only the 68 files confirmed
# byte-identical to a real upstream path (openmv-manifest.tsv) are
# fetched; the other 14 stay embedded as upy-android's own sources,
# same tier as camera_module.cpp. ulab, by contrast, really is clean: its
# whole code/ directory verified byte-identical to the pinned upstream
# commit (minus two build-integration files this project doesn't use).
#
# Regenerated via upy-android/native-bringup/micropython_embed.mk +
# upy-android/native-bringup/apply-overrides.sh, the same two-step
# process the real project's own dev workflow uses --
# app/src/main/cpp/micropython_embed/ is NOT embedded in this repo either
# (it's entirely reconstructable from the steps above), it's generated
# fresh at build time.
#
# Used in:
# - github workflows
# - local builds
#
# Local debug build:
#   docker build --target build -t upy-android .
#   id=$(docker create upy-android)
#   docker cp "$id":/output ./upy-android_apk
#   docker rm "$id"
#
# Local release build (debug-key-signed so it's installable -- NOT a
# production signing identity, see app/build.gradle.kts):
#   docker build --target build --build-arg BUILD_TYPE=release -t upy-android .
#   (then the same docker create/cp/rm steps)
#
# Tested:
# - amd64/ubuntu
#

FROM ubuntu:24.04 AS tools

# Pinned upstream commits -- every genuine third-party dependency, none
# vendored into this repo's own git history (see header comment above).
ENV MICROPYTHON_SHA=52b5fbcb4b7961356c83ba0c533ee42acdfa0169
ENV OPENMV_SHA=818f111d49effc98c0c6361f8fe85cb68deac9eb
ENV ULAB_SHA=01ad8a5fff89d7aeb68ef1827d1ba8f83f6462a7

ARG DEST_DIR=/root
WORKDIR ${DEST_DIR}

RUN apt-get update && apt-get upgrade -y

RUN apt-get install -y apt-utils

RUN apt-get install -y \
      git wget unzip curl ca-certificates openjdk-21-jdk-headless \
      build-essential python3

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
# 30.0.16248370) -- all version numbers below read directly off a real,
# already-working local install (sdkmanager --list_installed), not
# guessed. The cmdline-tools download build number is the one part of
# this file most likely to need bumping over time -- check
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

# upy-android's own sources -- embedded in this repo, not cloned (see
# header comment). This includes the 14 my-overrides/openmv/ files with
# no fetchable upstream equivalent, but NOT the 68 genuinely-vendored
# ones, and NOT my-overrides/ulab/ at all -- those are populated from the
# clones above, next.
COPY --chown=devuser:devuser upy-android /home/devuser/upy-android
COPY --chown=devuser:devuser tools/docker/openmv-manifest.tsv /home/devuser/openmv-manifest.tsv

WORKDIR /home/devuser/upy-android/native-bringup

# Populate my-overrides/openmv/ with the 68 confirmed-pristine files from
# the pinned OpenMV clone, per-file (not a directory copy -- the manifest
# maps this flat directory's filenames back to their real, differently
# structured upstream paths, e.g. lib/imlib/blob.c -> blob.c). The 14
# files already embedded (link_stubs.c, board_config.h, etc.) are left
# untouched.
RUN while IFS=$(printf '\t') read -r src dest; do \
        cp "/home/devuser/openmv/$src" "my-overrides/openmv/$dest"; \
    done < /home/devuser/openmv-manifest.tsv

# Populate my-overrides/ulab/ wholesale -- confirmed byte-identical to
# the pinned ulab clone's code/ directory (minus micropython.cmake/
# micropython.mk, its generic "user C module" build integration, which
# this project's own hand-rolled CMakeLists.txt doesn't use).
RUN mkdir -p my-overrides/ulab \
    && cp /home/devuser/ulab/code/*.c /home/devuser/ulab/code/*.h my-overrides/ulab/ \
    && find /home/devuser/ulab/code -mindepth 1 -maxdepth 1 -type d -exec cp -r {} my-overrides/ulab/ \;

# Regenerate the MicroPython embed-port tree fresh from the pinned
# upstream clone, then reapply our own override/new files on top --
# same two-step process the real project's own dev workflow uses.
# MICROPYTHON_TOP is overridden on the command line (a plain `=`
# assignment in micropython_embed.mk, not `override`, so this wins) --
# no source file needs patching for this container's own paths.
RUN rm -rf build-embed micropython_embed \
    && make -f micropython_embed.mk MICROPYTHON_TOP=/home/devuser/micropython \
    && ./apply-overrides.sh

# Sync the freshly-regenerated tree into the app's own source tree,
# replacing whatever came along in the COPY above -- this is what makes
# the build genuinely reproducible from upstream + our own patches,
# rather than silently depending on a pre-generated snapshot.
RUN rm -rf ../app/src/main/cpp/micropython_embed \
    && cp -r micropython_embed ../app/src/main/cpp/micropython_embed

WORKDIR /home/devuser/upy-android

RUN echo "sdk.dir=${ANDROID_SDK_ROOT}" > local.properties

RUN chmod +x ./gradlew

RUN if [ "$BUILD_TYPE" = "release" ]; then \
        ./gradlew :app:assembleRelease; \
    else \
        ./gradlew :app:assembleDebug; \
    fi

RUN if [ "$BUILD_TYPE" = "release" ]; then \
        cp app/build/outputs/apk/release/app-release.apk /output/upy-release.apk; \
    else \
        cp app/build/outputs/apk/debug/app-debug.apk /output/upy-debug.apk; \
    fi

# not truncated
