cmake_minimum_required(VERSION 3.22.1)
project(upy_engine)

# Matches the flags validated standalone in native-bringup/ (see project
# memory) -- CORE_FEATURES level, rooted VfsPosix + os module, crash-mitigation
# overrides in micropython_embed/port/*.c and shared/runtime/interrupt_char.c.
file(GLOB MPY_PY_SOURCES micropython_embed/py/*.c)
file(GLOB MPY_PORT_SOURCES micropython_embed/port/*.c)
file(GLOB MPY_SHARED_SOURCES micropython_embed/shared/runtime/*.c)

# Explicit list (MPY_EXTMOD_SOURCES), not a glob -- generated fresh
# alongside the extmod files themselves by native-bringup's
# embed-android.mk (android-extmod-package target), from the exact same
# manifest that drives which files get copied in. Self-documents exactly
# which extmod modules this project uses (json/os/random/re/time/
# binascii/vfs) and catches a missing or stray file at configure time
# instead of silently globbing whatever happens to be on disk.
include(micropython_embed/extmod/sources.cmake)

# ulab (numpy/scipy-like numerical extension, not a core MicroPython
# module, see native-bringup's micropython_embed.mk) -- its tree is up to
# 4 path components deep (e.g. ulab/numpy/fft/fft.c), unlike the other
# globs above which are intentionally shallow. GLOB_RECURSE is safe here
# since micropython_embed/ulab/ contains nothing else that's textually
# #include'd into a sibling file the way extmod/lib/re1.5/*.c or
# modtime_android.c are (re1.5 doesn't even reach this file -- it's not
# globbed at all, see sources.cmake's own generation in embed-android.mk).
file(GLOB_RECURSE MPY_ULAB_SOURCES micropython_embed/ulab/*.c)

# OpenMV imlib/py_image (machine vision, not a core MicroPython module,
# same tier as ulab -- see native-bringup's micropython_embed.mk and
# SESSION_STATE.yaml's "vendor unwired"/"wire up in native-bringup"
# commits). Flat directory, non-recursive glob is correct and sufficient
# -- unlike ulab, nothing under micropython_embed/openmv/ is nested more
# than one level except ulab-shim/ulab/code/ndarray.h, a header only, not
# a .c file this glob would need to reach.
file(GLOB MPY_OPENMV_SOURCES micropython_embed/openmv/*.c)

# camera_module.cpp/display_module.cpp/imu_module.cpp/android_module.cpp
# -- OUR OWN native modules, NOT vendored OpenMV source, so they live
# here directly rather than under micropython_embed/openmv/ (see
# SESSION_STATE.yaml's camera/display native-code-location scoping
# discussion, and the py_imu.c entries for why imu_module.cpp is a
# from-scratch module too). android_module.cpp nests imu_module.cpp's
# module struct (and, over time, proximity/light/touch) under a single
# top-level `android` module -- see android_module.cpp's own header
# comment.
add_library(upy_engine SHARED
    engine_jni.cpp
    camera_module.cpp
    display_module.cpp
    imu_module.cpp
    android_module.cpp
    tf_module.cpp
    ${MPY_PY_SOURCES}
    ${MPY_PORT_SOURCES}
    ${MPY_SHARED_SOURCES}
    ${MPY_EXTMOD_SOURCES}
    ${MPY_ULAB_SOURCES}
    ${MPY_OPENMV_SOURCES}
)

target_include_directories(upy_engine PRIVATE
    .
    micropython_embed
    micropython_embed/port
    micropython_embed/ulab
    micropython_embed/openmv
    micropython_embed/openmv/ulab-shim
    litert/include
)

# CMSIS_MCU_H: see my-overrides/openmv/board_config.h -- must be a command-
# line define, not a #define inside board_config.h itself (imlib.h
# includes fmath.h, which needs it, BEFORE including board_config.h).
#
# ULAB_CONFIG_FILE: see my-overrides/ulab/ulab_config.h -- ulab.h
# `#include`s it under `#if defined(ULAB_CONFIG_FILE)`, so, same as
# CMSIS_MCU_H, it must be a command-line define (ulab.h has no
# built-in ULAB_CONFIG_FILE fallback to redefine).
target_compile_definitions(upy_engine PRIVATE
    CMSIS_MCU_H="cmsis_mcu_stub.h"
    ULAB_CONFIG_FILE="ulab_config.h"
    # tf_module.cpp's hw_nnapi check (android.tf.info()) calls NNAPI
    # device-enumeration functions that need API 29, guarded by
    # __builtin_available -- but without this define, Bionic's headers
    # use "strict" availability mode (android/versioning.h, confirmed
    # by reading it directly, not guessed) where calling any above-
    # minSdk API is a hard BUILD ERROR regardless of any runtime guard.
    # This is the NDK's own documented mechanism for exactly this case
    # (https://developer.android.com/ndk/guides/using-newer-apis) --
    # switches to weak-linked symbols + __builtin_available-checked
    # calls actually compiling. Project-wide, not scoped to tf_module.cpp
    # specifically, matching this file's existing CMSIS_MCU_H/
    # ULAB_CONFIG_FILE pattern -- harmless for every other file, none of
    # which reference any API-level-gated symbol.
    __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
)

target_compile_options(upy_engine PRIVATE
    -Wall
    -Og
    -fno-common
)

# LiteRT (android.tf module, see SESSION_STATE.yaml's design discussion)
# -- this project's first PREBUILT BINARY dependency (see NOTICE.md),
# fetched by native-bringup/fetch-litert.sh (run it before building if
# app/src/main/cpp/litert/ doesn't exist -- same "populate before build"
# step as micropython_embed/'s own apply-overrides.sh, not auto-run by
# CMake itself). IMPORTED SHARED, not a source target: libLiteRt.so is a
# real prebuilt .so, not something this project compiles.
add_library(litert SHARED IMPORTED)
set_target_properties(litert PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/litert/lib/libLiteRt.so
)

# camera2ndk/mediandk: NDK Camera2 API used by camera_module.cpp (stable
# public NDK libs since API 24, no vendoring needed -- same libs the
# earlier camera probe already confirmed link/work, see SESSION_STATE.yaml).
# android: ANativeWindow_fromSurface() (engine_jni.cpp), the rest of the
# ANativeWindow_* API (display_module.cpp), and ASensorManager_*/
# ASensorEventQueue_*/ALooper_* (imu_module.cpp) -- all exported from
# libandroid.so, no separate library needed for the sensor API.
# neuralnetworks: android.tf.info()'s hw_nnapi field (tf_module.cpp) --
# libneuralnetworks.so needs API 27 (confirmed by reading
# NeuralNetworks.h directly, not assumed), which is why minSdk was
# bumped 26->27 (see build.gradle.kts's own comment) specifically to
# make this a safe, eager, direct link -- the library is guaranteed
# present on every device this app can install on. The specific
# device-enumeration functions used are still API 29-only regardless,
# guarded by a runtime check in tf_module.cpp, not a build-time one.
target_link_libraries(upy_engine log m camera2ndk mediandk android litert neuralnetworks)

# Linker map for libupy_engine.so -- static-link internals only (which
# MicroPython/ulab/openmv/our own object files pulled in, section sizes,
# confirms prebuilt deps like libLiteRt.so show up as NEEDED). Does NOT
# cover what's inside a prebuilt dependency itself, or what AGP packages
# into the final APK from an AAR's jni/ folder (that's a Gradle/AGP
# packaging concern, not a link-map one -- see SESSION_STATE.yaml's
# litert-api/libLiteRtClGlAccelerator.so findings, both caught via
# `unzip -l`/`nm -D` on the real APK, not a map file). Generated fresh
# per build under app/.cxx/ (already gitignored, not committed), same
# ephemeral treatment as every other build artifact.
target_link_options(upy_engine PRIVATE
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/upy_engine.map
)
