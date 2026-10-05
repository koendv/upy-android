cmake_minimum_required(VERSION 3.22.1)
project(upy_engine)

file(GLOB MPY_PY_SOURCES micropython_embed/py/*.c)
file(GLOB MPY_PORT_SOURCES micropython_embed/port/*.c)
file(GLOB MPY_SHARED_SOURCES micropython_embed/shared/runtime/*.c)

# Explicit list, not a glob: generated alongside the extmod files
# themselves by native-bringup's embed-android.mk, so a missing or
# stray file fails at configure time instead of silently globbing
# whatever is on disk.
include(micropython_embed/extmod/sources.cmake)

# ulab's tree is nested up to 4 levels deep (e.g. ulab/numpy/fft/fft.c),
# unlike the other globs here. GLOB_RECURSE is safe: nothing under it
# is #include'd into a sibling file the way some other globbed trees are.
file(GLOB_RECURSE MPY_ULAB_SOURCES micropython_embed/ulab/*.c)

# OpenMV imlib/py_image is flat, so a plain non-recursive glob is enough.
file(GLOB MPY_OPENMV_SOURCES micropython_embed/openmv/*.c)

# AprilTag library (OpenMV's lib/apriltag submodule). Only the sources its
# own apriltag.mk lists are copied in, see native-bringup/populate-vendor.sh.
file(GLOB MPY_APRILTAG_SOURCES micropython_embed/apriltag/*.c micropython_embed/apriltag/common/*.c)

# micropython android port only, not vendored OpenMV source.
add_library(upy_engine SHARED
    engine_jni.cpp
    camera_module.cpp
    camera_jni_bridge.cpp
    display_module.cpp
    imu_module.cpp
    android_module.cpp
    settings_module.cpp
    litert_module.cpp
    litert_jni_bridge.cpp
    tflite_module.cpp
    mediastore_module.cpp
    mediastore_jni_bridge.cpp
    fileprovider_module.cpp
    fileprovider_jni_bridge.cpp
    location_module.cpp
    location_jni_bridge.cpp
    mqtt_module.cpp
    mqtt_jni_bridge.cpp
    ${MPY_PY_SOURCES}
    ${MPY_PORT_SOURCES}
    ${MPY_SHARED_SOURCES}
    ${MPY_EXTMOD_SOURCES}
    ${MPY_ULAB_SOURCES}
    ${MPY_OPENMV_SOURCES}
    ${MPY_APRILTAG_SOURCES}
)

target_include_directories(upy_engine PRIVATE
    .
    micropython_embed
    micropython_embed/port
    micropython_embed/ulab
    micropython_embed/openmv
    micropython_embed/openmv/ulab-shim
    micropython_embed/apriltag
    micropython_embed/apriltag/common
    litert/include
    tflite/include
)

# Both must be command-line defines, not #defines in the header itself:
# imlib.h includes fmath.h (which needs CMSIS_MCU_H) before it includes
# board_config.h, and ulab.h has no built-in ULAB_CONFIG_FILE fallback
# to redefine.
target_compile_definitions(upy_engine PRIVATE
    CMSIS_MCU_H="cmsis_mcu_stub.h"
    ULAB_CONFIG_FILE="ulab_config.h"
)

target_compile_options(upy_engine PRIVATE
    -Wall
    -Og
    -fno-common
)

# Must be a command-line define here too: py_crc.c's #if MICROPY_PY_CRC
# guard comes before its own #include "py/runtime.h", so a define in
# mpconfigport.h is never visible in time.
set_source_files_properties(micropython_embed/openmv/py_crc.c
    PROPERTIES
    COMPILE_DEFINITIONS "MICROPY_PY_CRC=1"
)

# libLiteRt.so is a real prebuilt .so (see NOTICE.html for licensing;
# extracted from the litert AAR by build.gradle.kts's extractLitert
# task), not something this project compiles. Headers: litert/include/,
# committed, see litert/README.md.
#
# tflite_module.cpp links this SAME .so (via upy_engine's link line
# below), not a second TFLite build: libLiteRt.so already exports the
# classic TfLite* C API (confirmed via nm) alongside LiteRT's own
# LiteRt* API, since LiteRT is built from its own copy of the TFLite
# source. One copy of TFLite in the process, not two colliding in the
# dynamic symbol table -- see tflite/README.md and session-state for
# why this matters. tflite_module.cpp and litert_module.cpp/
# LiteRtShim.kt remain independent source files (zero cross-links
# between the two modules' own code); they just happen to share this
# one upstream native library, the same way two modules might both use
# libc.
add_library(litert SHARED IMPORTED)
set_target_properties(litert PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/litert/lib/libLiteRt.so
)

# android: stable NDK lib for the display and sensor APIs, no
# vendoring needed.
# This link line's only real purpose is getting AGP to package
# libLiteRt.so into the APK's jniLibs, which LiteRtShim.kt needs at
# runtime.
target_link_libraries(upy_engine log m android litert)

# Linker map for libupy_engine.so. Doesn't cover what's inside a
# prebuilt dependency, or what AGP packages into the APK from an AAR's
# jni/ folder. Generated fresh per build under app/.cxx/, not committed.
target_link_options(upy_engine PRIVATE
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/upy_engine.map
)

# Same per-file flags as OpenMV's lib/apriltag/apriltag.mk: its config hook
# (my-overrides/openmv/apriltag_config.h) and its upstream-warning suppressions.
#
# micropython_embed/openmv/apriltag.c (imlib_find_rects/imlib_find_apriltags)
# must get this too, even though it's globbed into MPY_OPENMV_SOURCES, not
# MPY_APRILTAG_SOURCES: it includes apriltag.h directly and calls
# apriltag_detector_create()/zarray_add() etc. Without APRILTAG_HAVE_CONFIG
# here, this TU saw apriltag_detector_t with the *default* (upstream)
# APRILTAG_ENABLE_PROFILE/APRILTAG_ENABLE_PTHREADS values (1, 1) instead of
# apriltag_config.h's (0, 0) -- a different struct layout (extra
# timeprofile_t*/pthread_mutex_t fields) than every other AprilTag TU, which
# shifted tag_families to the wrong offset and crashed with "za != NULL" in
# zarray_add (reading what apriltag_detector_create() actually wrote to the
# following field, left zero). Confirmed on-device 2026-10-02, see
# session-state.
set_source_files_properties(${MPY_APRILTAG_SOURCES} micropython_embed/openmv/apriltag.c
    PROPERTIES
    COMPILE_DEFINITIONS "APRILTAG_HAVE_CONFIG"
    COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-sign-compare;-Wno-missing-braces;-Wno-unused-function;-Wno-format;-Wno-double-promotion"
)
