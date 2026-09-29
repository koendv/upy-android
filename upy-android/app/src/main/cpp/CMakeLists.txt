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
    display_module.cpp
    imu_module.cpp
    android_module.cpp
    settings_module.cpp
    litert_module.cpp
    litert_jni_bridge.cpp
    mediastore_module.cpp
    mediastore_jni_bridge.cpp
    fileprovider_module.cpp
    fileprovider_jni_bridge.cpp
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
add_library(litert SHARED IMPORTED)
set_target_properties(litert PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/litert/lib/libLiteRt.so
)

# camera2ndk/mediandk/android: stable NDK libs for the camera, display,
# and sensor APIs, no vendoring needed.
# This link line's only real purpose is getting AGP to package
# libLiteRt.so into the APK's jniLibs, which LiteRtShim.kt needs at
# runtime.
target_link_libraries(upy_engine log m camera2ndk mediandk android litert)

# Linker map for libupy_engine.so. Doesn't cover what's inside a
# prebuilt dependency, or what AGP packages into the APK from an AAR's
# jni/ folder. Generated fresh per build under app/.cxx/, not committed.
target_link_options(upy_engine PRIVATE
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/upy_engine.map
)

# Same per-file flags as OpenMV's lib/apriltag/apriltag.mk: its config hook
# (my-overrides/openmv/apriltag_config.h) and its upstream-warning suppressions.
set_source_files_properties(${MPY_APRILTAG_SOURCES}
    PROPERTIES
    COMPILE_DEFINITIONS "APRILTAG_HAVE_CONFIG"
    COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-sign-compare;-Wno-missing-braces;-Wno-unused-function;-Wno-format;-Wno-double-promotion"
)
