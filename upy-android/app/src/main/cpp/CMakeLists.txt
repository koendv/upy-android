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
)

target_compile_options(upy_engine PRIVATE
    -Wall
    -Og
    -fno-common
)

# camera2ndk/mediandk: NDK Camera2 API used by camera_module.cpp (stable
# public NDK libs since API 24, no vendoring needed -- same libs the
# earlier camera probe already confirmed link/work, see SESSION_STATE.yaml).
# android: ANativeWindow_fromSurface() (engine_jni.cpp), the rest of the
# ANativeWindow_* API (display_module.cpp), and ASensorManager_*/
# ASensorEventQueue_*/ALooper_* (imu_module.cpp) -- all exported from
# libandroid.so, no separate library needed for the sensor API.
target_link_libraries(upy_engine log m camera2ndk mediandk android)
