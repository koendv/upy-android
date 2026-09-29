# syntax=docker/dockerfile:1

# Build upy-android's upy-debug.apk (or a simple debug-key-signed
# upy-release.apk) inside Docker. MicroPython running as an Android app.
#
# Only upy-android/'s own sources are in this repo. Every third-party
# dependency (MicroPython, OpenMV, ulab, LiteRT) is fetched by the Gradle
# build itself, at the versions pinned in upy-android/upstream.properties,
# so a Docker build and a local ./gradlew build run the same steps.
#
# see session-state: Dockerfile#openmv_vendoring_audit
#
# Used in:
# - github workflows
# - local builds
#
# Two-phase locally: build "tools" once (rebuild only when a pinned SDK/
# NDK/cmake version above changes), then "build" every time after.
# BuildKit's own layer cache already makes an unchanged "tools" stage a
# no-op, this split just documents/names that so it's not accidental.
# The "build" stage's compile step is BuildKit-cache-mounted (Gradle
# state, upstream clones, generated native inputs, app/.cxx, app/build),
# so a rebuild after a small source change only recompiles/relinks what
# actually changed. Requires a BuildKit-enabled `docker build` (the
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

ARG DEST_DIR=/root
WORKDIR ${DEST_DIR}

RUN apt-get update

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

# upy-android's own sources. Third-party sources are not part of this
# COPY: Gradle fetches them at the commits pinned in upstream.properties
# (see native-bringup/fetch-upstream.sh and app/build.gradle.kts).
COPY --chown=devuser:devuser upy-android /home/devuser/upy-android

WORKDIR /home/devuser/upy-android

RUN echo "sdk.dir=${ANDROID_SDK_ROOT}" > local.properties

RUN chmod +x ./gradlew

# ONE-TIME: generate app/gradle.lockfile to pin down dependencies
#RUN ./gradlew :app:dependencies --write-locks && \
#    find . -name 'gradle.lockfile' -o -name '*gradle.lockfile*' && \
#    cp app/gradle.lockfile /output/gradle.lockfile

# Cache-mounted: Gradle's caches and task state, the upstream clones,
# every generated native input, and CMake/Ninja's object files all
# survive across `docker build` runs, so a small change only rebuilds
# what depends on it. A cache mount's contents are not part of the image
# once this RUN ends, so the APK copy must happen in this same RUN.
RUN --mount=type=cache,target=/home/devuser/.gradle,uid=1000,gid=1000 \
    --mount=type=cache,target=/home/devuser/upy-android/.gradle,uid=1000,gid=1000 \
    --mount=type=cache,target=/home/devuser/upy-android/upstream,uid=1000,gid=1000 \
    --mount=type=cache,target=/home/devuser/upy-android/native-bringup/vendor,uid=1000,gid=1000 \
    --mount=type=cache,target=/home/devuser/upy-android/app/src/main/cpp/micropython_embed,uid=1000,gid=1000 \
    --mount=type=cache,target=/home/devuser/upy-android/app/src/main/cpp/litert,uid=1000,gid=1000 \
    --mount=type=cache,target=/home/devuser/upy-android/app/src/main/jniLibs,uid=1000,gid=1000 \
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
