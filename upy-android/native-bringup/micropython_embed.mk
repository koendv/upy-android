# This file is part of the MicroPython project, http://micropython.org/
# The MIT License (MIT)
# Copyright (c) 2022-2023 Damien P. George

# Set the location of the top of the MicroPython repository.
MICROPYTHON_TOP = /home/koen/src/repos/upy-android/upstream/micropython

# There is only one mpconfigport.h in this project: ../app/src/main/cpp/
# mpconfigport.h, the file the real app is actually compiled with (its
# own directory is first on CMakeLists.txt's include path). A second,
# separately-maintained copy used to live here too, feeding only this
# qstr-scan step. The two drifted (MICROPY_FLOAT_IMPL FLOAT vs DOUBLE)
# without anyone noticing, since nothing but a rebuild's genhdr output
# would have revealed it. embed.mk's own mkrules.mk hard-requires
# mpconfigport.h to exist as a real prerequisite file (resolved by GNU
# Make's own dependency tracking, not by any CFLAGS -I search). vpath
# tells Make where to actually find it without a second copy.
vpath mpconfigport.h ../app/src/main/cpp

# extmod VFS/os/json/random/re/time/binascii: embed.mk's own SRC_QSTR
# only scans py/*.c, it has no idea this project needs these extmod
# modules at all. Split out into its own file (embed-android.mk), which
# also handles fetching the actual files fresh from $(MICROPYTHON_TOP)
# at generation time (they used to be vendored in my-overrides/extmod/,
# removed once every one was confirmed byte-identical to upstream).
#
# Must come before the `include embed.mk` below: mkrules.mk (pulled in
# transitively by that include) defines a rule whose prerequisite list is
# `$(SRC_QSTR)`, and prerequisite lists expand at *parse time* in GNU
# Make, not at build time. An append placed after the include is too
# late; the rule has already captured SRC_QSTR's pre-append value.
include embed-android.mk

# ulab (https://github.com/v923z/micropython-ulab): numpy/scipy-like
# numerical computing extension. Not a core MicroPython module, normally
# integrated via the "user C modules" mechanism (USER_C_MODULES=, a
# code/micropython.mk fragment the top-level Makefile includes), which
# this hand-rolled build doesn't use. Point qstr-scanning directly at
# the pristine upstream copy (vendor/ulab/, fetched fresh at build time,
# see Dockerfile/.gitignore, never committed to this repo's own git
# history). There is no separate "pristine upstream original" to point
# at the way extmod/*.c files above do, since embed.mk has no knowledge
# of ulab at all.
#
# Deliberately staged in two steps: this SRC_QSTR wiring lands first, on
# its own, so `make -f micropython_embed.mk` can be run and genhdr/
# qstrdefs output inspected for ulab-specific qstrs (e.g. Q(ndarray),
# Q(linspace)) before any of these files are added to an actual compile
# glob. Getting this step wrong surfaces as a clean-looking qstr
# generation pass followed by 33 simultaneous "MP_QSTR_* undeclared"
# compile errors, much harder to debug in one shot than confirming
# genhdr content first.
SRC_QSTR += vendor/ulab/ndarray.c
SRC_QSTR += vendor/ulab/ndarray_operators.c
SRC_QSTR += vendor/ulab/ndarray_properties.c
SRC_QSTR += vendor/ulab/ulab.c
SRC_QSTR += vendor/ulab/ulab_tools.c
SRC_QSTR += vendor/ulab/user/user.c
SRC_QSTR += vendor/ulab/utils/utils.c
SRC_QSTR += vendor/ulab/numpy/numpy.c
SRC_QSTR += vendor/ulab/numpy/approx.c
SRC_QSTR += vendor/ulab/numpy/bitwise.c
SRC_QSTR += vendor/ulab/numpy/compare.c
SRC_QSTR += vendor/ulab/numpy/create.c
SRC_QSTR += vendor/ulab/numpy/filter.c
SRC_QSTR += vendor/ulab/numpy/numerical.c
SRC_QSTR += vendor/ulab/numpy/poly.c
SRC_QSTR += vendor/ulab/numpy/stats.c
SRC_QSTR += vendor/ulab/numpy/transform.c
SRC_QSTR += vendor/ulab/numpy/vector.c
SRC_QSTR += vendor/ulab/numpy/carray/carray.c
SRC_QSTR += vendor/ulab/numpy/carray/carray_tools.c
SRC_QSTR += vendor/ulab/numpy/fft/fft.c
SRC_QSTR += vendor/ulab/numpy/fft/fft_tools.c
SRC_QSTR += vendor/ulab/numpy/io/io.c
SRC_QSTR += vendor/ulab/numpy/linalg/linalg.c
SRC_QSTR += vendor/ulab/numpy/linalg/linalg_tools.c
SRC_QSTR += vendor/ulab/numpy/ndarray/ndarray_iter.c
SRC_QSTR += vendor/ulab/numpy/random/random.c
SRC_QSTR += vendor/ulab/scipy/scipy.c
SRC_QSTR += vendor/ulab/scipy/integrate/integrate.c
SRC_QSTR += vendor/ulab/scipy/linalg/linalg.c
SRC_QSTR += vendor/ulab/scipy/optimize/optimize.c
SRC_QSTR += vendor/ulab/scipy/signal/signal.c
SRC_QSTR += vendor/ulab/scipy/special/special.c

# extmod/modtime.c #includes MICROPY_PY_TIME_INCLUDEFILE ("port/
# modtime_android.c", mpconfigport.h), this project's own file, no
# upstream original, unlike every other SRC_QSTR entry above. The
# qstr-scan pass above (of the pristine
# $(MICROPYTHON_TOP)/extmod/modtime.c) preprocesses with this same
# CFLAGS, so "port/modtime_android.c" needs to resolve to something
# that exists before apply-overrides.sh has run (which is what
# actually populates micropython_embed/port/, too late for this scan).
# Point CFLAGS at my-overrides/ itself (always present, hand-
# maintained, not generated) so "port/modtime_android.c" resolves to
# my-overrides/port/modtime_android.c during the scan. The real build
# (both native-bringup's own compile step and the real app's CMake
# build) resolves the same "port/modtime_android.c" via
# -Imicropython_embed once apply-overrides.sh has copied it to
# micropython_embed/port/. Same ordering rule as SRC_QSTR above: must
# come before `include embed.mk`, since CFLAGS is captured at
# mkrules.mk's parse point.
CFLAGS += -Imy-overrides
# Same role as -Imy-overrides above, but for the vendor/ tree. Needed
# by ulab-shim/ulab/code/ndarray.h's own "ulab/ndarray.h" passthrough
# (see the -Imy-overrides/openmv/ulab-shim comment below), which must
# resolve to vendor/ulab/ndarray.h now that ulab is split vendor/+
# my-overrides/ instead of one flat directory.
CFLAGS += -Ivendor
# ulab's own files #include each other relative to their code/ root
# (e.g. ulab.c: #include "ndarray.h", "numpy/numpy.h"). One -I at the
# pristine vendor/ulab/ root (mirroring their code/ root exactly) is
# enough for every such include across all 33 files, no per-file or
# per-subdirectory -I entries needed. Same before-the-include ordering
# requirement as every other CFLAGS/SRC_QSTR line above.
# -Imy-overrides/ulab is still needed separately, for
# ULAB_CONFIG_FILE's own "ulab_config.h" (this project's own file, not
# vendored, see CMakeLists.txt's own target_compile_definitions).
CFLAGS += -Imy-overrides/ulab
CFLAGS += -Ivendor/ulab

# OpenMV imlib/py_image: machine vision extension, same "not a core
# MicroPython module" tier as ulab above. my-overrides/openmv/ holds
# this project's own Camera2-era replacement headers (arm_math.h,
# framebuffer.h, etc.); vendor/openmv/ holds the pristine upstream
# imlib/common/modules/*.c, flat, not mirrored into subdirectories,
# confirmed safe before vendoring (every #include across the tree is
# unqualified except two dead-code ones behind #if OMV_PROFILER_ENABLE,
# never defined).
# Only the modules/*.c files (py_image.c etc) actually use MP_QSTR_*.
# The imlib/common files underneath are C-only, no qstr surface of
# their own, but SRC_QSTR only needs the ones with real usage, same as
# ulab's own SRC_QSTR list above only listing user-facing files, not
# every internal .c ulab vendors.
SRC_QSTR += vendor/openmv/py_image.c
SRC_QSTR += vendor/openmv/py_helper.c
SRC_QSTR += vendor/openmv/py_image_descriptor.c
SRC_QSTR += vendor/openmv/py_imageio.c
SRC_QSTR += vendor/openmv/py_image_stats.c
# py_clock.c (time.clock(), OpenMV script compatibility). Not imlib/
# vision code, but same vendored location and same "separate
# translation unit needs its own SRC_QSTR entry" reasoning: its
# tick()/fps()/avg()/reset()/Clock qstrs live in this file's own
# locals_dict_table, not in modtime_android.c (which only references
# &py_clock_type and the exposed `clock` name, see that file's own
# MICROPY_PY_TIME_EXTRA_GLOBALS entry).
SRC_QSTR += vendor/openmv/py_clock.c
# py_gif.c (import gif, IMLIB_ENABLE_IMAGE_FILE_IO, see
# imlib_config.h's own comment). Same vendored-location reasoning as
# the other modules/*.c files above. The qstr-scan reads this pristine
# vendor/ copy (fine: my-overrides/openmv/py_gif.c's own patch changes
# no MP_QSTR_*-visible names, only py_gif_open()'s internal framebuffer
# handling, see that file's own header comment); the actual compile
# picks up the patched override instead, via apply-overrides.sh's
# cp -r my-overrides/openmv/. (after vendor/openmv/) letting same-named
# files win. gif.c itself (lib/imlib/) needs no SRC_QSTR entry, same
# "C-only, no qstr surface" reasoning as the comment above.
SRC_QSTR += vendor/openmv/py_gif.c
# py_mjpeg.c (import mjpeg, same IMLIB_ENABLE_IMAGE_FILE_IO gate as
# gif). Same "qstr-scan reads the pristine vendor/ copy, the real
# compile picks up my-overrides/openmv/py_mjpeg.c's patched override"
# reasoning as py_gif.c's own comment just above.
SRC_QSTR += vendor/openmv/py_mjpeg.c
# py_crc.c (import crc, MICROPY_PY_CRC, see mpconfigport.h's own
# comment): crc16()/crc32(), no patch needed at all (pure arithmetic,
# no framebuffer/hardware dependency unlike gif/mjpeg). omv_crc.c
# (common/) needs no SRC_QSTR entry, same "C-only, no qstr surface"
# reasoning as gif.c/mjpeg.c above.
SRC_QSTR += vendor/openmv/py_crc.c
# Must be a command-line define, not mpconfigport.h's own #define.
# py_crc.c's #if MICROPY_PY_CRC guard is its own first real line,
# before its own #include "py/runtime.h" (the only path mpconfigport.h
# would otherwise reach it through), same tflm_backend.cc-class bug,
# see mpconfigport.h's own comment for the full real-bug writeup.
CFLAGS += -DMICROPY_PY_CRC=1
# CMSIS_MCU_H: real boards point this at their vendor MCU header (e.g.
# stm32h7xx.h). Nothing compiled here needs real CMSIS SFR/intrinsic
# definitions once __ARM_ARCH is forced below 7/8 (see arm_math.h).
# cmsis_mcu_stub.h only needs to exist so the #include resolves. Must
# be on the command line, not a #define inside board_config.h: imlib.h
# includes fmath.h (which needs it) before it includes board_config.h,
# so a #define there would always be too late (see board_config.h).
CFLAGS += -DCMSIS_MCU_H='"cmsis_mcu_stub.h"'
# One -I each for this project's replacement headers and the vendored
# imlib/common/modules .c/.h files, same flat-directory reasoning as
# ulab's my-overrides/ulab + vendor/ulab pair above.
CFLAGS += -Imy-overrides/openmv
CFLAGS += -Ivendor/openmv
# py_image.c's own "ulab/code/ndarray.h" include doesn't resolve
# against this project's flattened ulab tree as-is.
# ulab-shim/ulab/code/ndarray.h is a one-line passthrough (#include
# "ulab/ndarray.h") bridging the two layouts. -Imy-overrides/openmv/
# ulab-shim makes py_image.c's own include resolve to the shim. The
# shim's own "ulab/ndarray.h" then needs -Ivendor (added above, same
# qualified-relative-include role, one for this project's own tree and
# one for the vendor tree) to resolve to vendor/ulab/ndarray.h, the
# real, post-rename file.
CFLAGS += -Imy-overrides/openmv/ulab-shim

# camera_module.cpp: this project's own native csi (camera) module, not
# vendored OpenMV code. Lives directly under app/src/main/cpp/, its
# real permanent location, not under my-overrides/ (never copied by
# apply-overrides.sh), so this SRC_QSTR entry points straight at the
# app tree, a real path. Path is relative to native-bringup/, where
# this Makefile runs.
SRC_QSTR += ../app/src/main/cpp/camera_module.cpp
CFLAGS += -I../app/src/main/cpp
# The qstr-scan pass preprocesses with the host gcc (embed.mk's own
# default), not the Android NDK clang wrapper the real build uses.
# Unlike every other vendored file so far, camera_module.cpp #includes
# genuine NDK-only headers (<camera/...>, <media/...>). Pointing CFLAGS
# at the real NDK sysroot was tried first and broke every other
# SRC_QSTR file in this same combined scan: CFLAGS here is one global
# list shared across the whole `gcc -E file1.c file2.c ...` invocation
# (this Makefile has no per-file CFLAGS mechanism), so the NDK's own
# libc headers (limits.h etc) collided with the host's, e.g.
# MB_LEN_MAX redefined. Fix: empty stub headers (qstr-stub/, just
# include guards, no declarations) that satisfy the #include lines
# without pulling in any real NDK system header. Safe because -E only
# macro-expands and resolves #includes, it never type-checks the
# ACameraManager* etc. bodies that follow, which is all a qstr scan needs.
CFLAGS += -Iqstr-stub

# display_module.cpp: this project's own native display module, same
# tier/location reasoning as camera_module.cpp just above (real app
# path, qstr-stub headers for its own NDK-only #includes,
# android/native_window.h, reusing the android/log.h stub
# camera_module.cpp already needed).
SRC_QSTR += ../app/src/main/cpp/display_module.cpp

# imu_module.cpp: this project's own native imu module, same
# tier/location reasoning as camera_module.cpp/display_module.cpp
# above (real app path, not vendored OpenMV code). Needs two new
# qstr-stub headers (android/sensor.h, android/looper.h) for its own
# NDK-only #includes, same empty-include-guard-only shape as the
# existing android/log.h and android/native_window.h stubs.
SRC_QSTR += ../app/src/main/cpp/imu_module.cpp

# android_module.cpp: this project's own native module nesting
# imu_module.cpp's module struct (and, over time, proximity/light/
# touch) under a single top-level `android` module. No new qstr-stub
# headers needed (only #includes py/runtime.h/py/obj.h, both already
# resolved by every other module's scan).
SRC_QSTR += ../app/src/main/cpp/android_module.cpp

# litert_module.cpp: this project's own native module (android.litert).
# Talks to litert-api's Kotlin surface through litert_jni_bridge.h
# (stddef.h/stdint.h only) for Environment/CompiledModel/TensorBuffer/
# run(). That part never touches LiteRT's native C headers directly
# (that direct-C-API approach was tf_module.cpp's/rt_module.cpp's,
# both since deleted).
#
# get_input_tensor_type()/get_output_tensor_type()/
# get_input_tensor_quantization()/get_output_tensor_quantization() do
# now #include LiteRT's own C API directly (litert/c/litert_common.h,
# litert_environment.h, litert_model.h, litert_model_types.h),
# reopening the .tflite file via LiteRtCreateModelFromFile for cheap,
# flatbuffer-only metadata (no interpreter/arena), bypassing Kotlin/JNI
# entirely for just this. Unlike camera_module.cpp's own -Iqstr-stub
# workaround above, these are plain, portable C99 headers with no
# NDK/Android-only #includes of their own, confirmed safe to add to
# this shared, host-gcc-preprocessed CFLAGS list directly, no stub
# headers needed.
CFLAGS += -I../app/src/main/cpp/litert/include
SRC_QSTR += ../app/src/main/cpp/litert_module.cpp

# settings_module.cpp: this project's own native module
# (android.settings). Same minimal case as android_module.cpp above
# (no new qstr-stub headers, only py/runtime.h/py/obj.h and
# settings_state.h, which is plain C++ with no system headers of its
# own).
SRC_QSTR += ../app/src/main/cpp/settings_module.cpp

# mediastore_module.cpp: this project's own native module
# (android.mediastore). Same minimal case as settings_module.cpp above
# (no new qstr-stub headers, only py/obj.h/py/runtime.h/py/mperrno.h
# and mediastore_jni_bridge.h, which uses only primitive C types, no
# system headers of its own). mediastore_jni_bridge.cpp is
# deliberately not listed here: it #includes <jni.h>, no qstr-stub
# exists for JNI headers, same reasoning as litert_jni_bridge.cpp/
# engine_jni.cpp's own exclusion from this list.
SRC_QSTR += ../app/src/main/cpp/mediastore_module.cpp

# fileprovider_module.cpp: this project's own native module
# (android.fileprovider). Same minimal case as mediastore_module.cpp
# above. fileprovider_jni_bridge.cpp is deliberately not listed here:
# it #includes <jni.h>, same exclusion reasoning as
# mediastore_jni_bridge.cpp above.
SRC_QSTR += ../app/src/main/cpp/fileprovider_module.cpp

# mqtt_module.cpp: this project's own native module (top-level umqtt).
# Same minimal case as mediastore_module.cpp above. mqtt_jni_bridge.cpp
# is deliberately not listed here: it #includes <jni.h>, same exclusion
# reasoning as mediastore_jni_bridge.cpp above.
SRC_QSTR += ../app/src/main/cpp/mqtt_module.cpp

# Include the main makefile fragment to build the MicroPython component.
include $(MICROPYTHON_TOP)/ports/embed/embed.mk
