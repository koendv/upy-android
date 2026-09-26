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
    settings_module.cpp
    tf_module.cpp
    rt_module.cpp
    litert_module.cpp
    litert_jni_bridge.cpp
    ${MPY_PY_SOURCES}
    ${MPY_PORT_SOURCES}
    ${MPY_SHARED_SOURCES}
    ${MPY_EXTMOD_SOURCES}
    ${MPY_ULAB_SOURCES}
    ${MPY_OPENMV_SOURCES}
    # tflm_backend.cc (patched -- see its own header comment) is NOT
    # picked up by MPY_OPENMV_SOURCES above (that glob only matches
    # *.c, this is a real .cc needing TFLM's own C++ headers) -- listed
    # explicitly, same tier as tf_module.cpp/rt_module.cpp just above.
    # Its own extra include path (tflm/) is scoped to just this one
    # file via set_source_files_properties below, not added project-wide.
    micropython_embed/openmv/tflm_backend.cc
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

# TFLM (TensorFlow Lite Micro) -- the real interpreter + reference-kernel
# backend behind `import ml`/`import tf` (py_ml.c/tflm_backend.cc, see
# mpconfigport.h's own MICROPY_PY_ML comment). Vendored pristine source
# at tflm/ (gitignored, fetched fresh at the pinned TFLM_SHA -- see
# .gitignore/Dockerfile), bypassing TFLM's own Makefile/toolchain
# machinery entirely, same "vendor + compile under our own build"
# pattern as ulab/imlib.
#
# Built as its OWN isolated static library, NOT folded into upy_engine's
# main source list: android.tf/android.rt/litert (all still alive per
# this project's established sequencing policy, see SESSION_STATE.yaml)
# already link a real TfLite*/tflite:: symbol surface from the prebuilt
# libLiteRt.so, and TFLM's own reference-kernel tree defines the SAME
# C++ namespace (tflite::) and C API names (TfLiteIntArrayCreate,
# TfLiteTypeGetName, etc.) under different (TF_LITE_STATIC_MEMORY)
# semantics -- a real collision risk, not hypothetical, since both end
# up loaded in the same process. -fvisibility=hidden here means none of
# TFLM's own symbols are ever exported into upy_engine.so's dynamic
# symbol table at all, even though they're statically linked in --
# that's the actual defense (not just the -Wl,--exclude-libs belt-and-
# suspenders on upy_engine's own link step below). See SESSION_STATE.yaml
# for the nm verification steps this was checked against, not just
# reasoned about.
file(GLOB_RECURSE TFLM_SOURCES tflm/*.cc tflm/*.c)
add_library(tflm STATIC ${TFLM_SOURCES})
target_include_directories(tflm PRIVATE
    tflm
    tflm/third_party/gemmlowp
    tflm/third_party/flatbuffers/include
    tflm/third_party/ruy
    tflm/third_party/kissfft
)
target_compile_options(tflm PRIVATE
    -O2
    -fvisibility=hidden
    -fvisibility-inlines-hidden
)
# MicroMutableOpResolver<113> and friends need real templates -- matches
# the c++17 standard this tree was already host-compile-verified against
# (see SESSION_STATE.yaml) rather than relying on whatever gnu++??
# default the NDK's own CMake toolchain file happens to pick.
target_compile_features(tflm PRIVATE cxx_std_17)
# TF_LITE_STATIC_MEMORY: the one load-bearing define from TFLM's own
# Makefile default BUILD_TYPE (confirmed by reading it directly, not
# guessed) -- TfLiteIntArray and other core structs have a genuinely
# different (fixed-size, no heap) layout under this macro. Must apply
# identically here AND on tflm_backend.cc below (both touch these
# structs), and must NOT apply to anything else in upy_engine, whose
# other files (tf_module.cpp/rt_module.cpp/litert_module.cpp) link
# against the prebuilt libLiteRt.so's own regular (non-static-memory)
# struct layout. -DNDEBUG/-DTF_LITE_STRIP_ERROR_STRINGS deliberately
# NOT added -- those are the Makefile's "release" BUILD_TYPE only; the
# default keeps real error strings, which is what makes tflm_backend.cc's
# RegisterDebugLogCallback(ml_backend_log_handler) routing actually
# useful (real messages in script output, not blanked-out ones).
target_compile_definitions(tflm PRIVATE
    TF_LITE_STATIC_MEMORY
    TF_LITE_DISABLE_X86_NEON
)

# tflm_backend.cc's own #include "tensorflow/lite/micro/..." lines need
# the tflm/ root -- scoped to just this one file, not added to
# upy_engine's own target_include_directories below, so the rest of
# upy_engine's sources (tf_module.cpp/rt_module.cpp/litert_module.cpp,
# which resolve their own tflite/c/*.h and litert/c/*.h via litert/include
# instead) never see TFLM's header tree at all. Same TF_LITE_STATIC_MEMORY
# define as the tflm target above -- see that target's own comment.
#
# MICROPY_PY_ML_TFLM must ALSO be a command-line define here, not just
# set in mpconfigport.h -- REAL BUG, caught by a real undefined-symbol
# link error, not spotted by inspection: unlike py_ml.c (whose own
# #if MICROPY_PY_ML guard comes AFTER its #include "py/runtime.h", so
# mpconfigport.h's define is already visible by then), tflm_backend.cc's
# #if MICROPY_PY_ML_TFLM is the file's very FIRST line, wrapping even
# its own #include "py/runtime.h" -- so mpconfigport.h's define (only
# reachable transitively THROUGH that include) is never seen in time,
# and the whole file's real content (including ml_backend_init_model
# and the other 3 functions py_ml.c calls) silently compiles out to an
# empty translation unit. No compile error, no warning -- only shows up
# as "undefined symbol: ml_backend_init_model" at link time.
# All four -I paths, matching the exact set the host-compile smoke test
# (SESSION_STATE.yaml) needed -- MicroMutableOpResolver<113>'s own
# AddXxx() declarations reach real kernel headers (e.g. kernels/
# pooling.h -> integer_ops/pooling.h -> gemmlowp's fixedpoint.h), not
# just forward declarations, so this file needs the same third_party
# surface as the tflm target itself, just scoped to this one file
# rather than project-wide.
# common-shim: tflm_backend.cc's own #include "common/omv_profiler.h"
# (its real upstream path) doesn't resolve against this project's
# flattened openmv/ tree -- same bridging pattern as ulab-shim/ulab/
# code/ndarray.h (see my-overrides/openmv/common-shim/common/
# omv_profiler.h's own header comment).
set_source_files_properties(micropython_embed/openmv/tflm_backend.cc
    PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/tflm;${CMAKE_CURRENT_SOURCE_DIR}/tflm/third_party/gemmlowp;${CMAKE_CURRENT_SOURCE_DIR}/tflm/third_party/flatbuffers/include;${CMAKE_CURRENT_SOURCE_DIR}/tflm/third_party/ruy;${CMAKE_CURRENT_SOURCE_DIR}/micropython_embed/openmv/common-shim"
    COMPILE_DEFINITIONS "TF_LITE_STATIC_MEMORY;TF_LITE_DISABLE_X86_NEON;MICROPY_PY_ML_TFLM=1"
    # REAL GAP, caught by the nm verification, not by inspection: this
    # file is the one that actually INSTANTIATES MicroMutableOpResolver
    # <113>'s template methods (typedef MicroOpsResolver + resolver->
    # AddAbs() etc) -- each TU that instantiates a template generates
    # its own weak-linkage copy, and this TU is compiled as part of
    # upy_engine (default visibility), not the tflm target (hidden) --
    # so without repeating the same visibility flags here, this file's
    # own instantiations still leaked tflite::/TfLiteIntArray/abort/
    # operator-delete symbols into libupy_engine.so's dynamic symbol
    # table (confirmed via nm -D, not assumed fixed once) regardless of
    # the tflm target's own -fvisibility=hidden.
    COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden"
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
target_link_libraries(upy_engine log m camera2ndk mediandk android litert neuralnetworks tflm)

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
    # Belt-and-suspenders on top of the tflm target's own
    # -fvisibility=hidden (see that target's own comment) -- tells the
    # linker to never export a symbol originating from libtflm.a into
    # upy_engine.so's dynamic symbol table, regardless of that symbol's
    # own visibility attribute.
    -Wl,--exclude-libs,libtflm.a
)
