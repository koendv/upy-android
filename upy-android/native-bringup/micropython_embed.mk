# This file is part of the MicroPython project, http://micropython.org/
# The MIT License (MIT)
# Copyright (c) 2022-2023 Damien P. George

# Set the location of the top of the MicroPython repository.
MICROPYTHON_TOP = /home/koen/src/repos/upy-android/upstream/micropython

# extmod VFS/os/json/random/re/time/binascii -- embed.mk's own SRC_QSTR
# only scans py/*.c, it has no idea this project needs these extmod
# modules at all. Split out into its own file (embed-android.mk) --
# also handles fetching the actual files fresh from $(MICROPYTHON_TOP)
# at generation time (they used to be vendored in my-overrides/extmod/;
# see SESSION_STATE.yaml for the per-file audit that confirmed every one
# byte-identical, and the commit that removed the vendored copies).
#
# MUST come before the `include embed.mk` below: mkrules.mk (pulled in
# transitively by that include) defines a rule whose prerequisite list is
# `$(SRC_QSTR)` -- and prerequisite lists expand at *parse time* in GNU
# Make, not at build time. An append placed after the include is too
# late; the rule has already captured SRC_QSTR's pre-append value.
include embed-android.mk

# ulab (https://github.com/v923z/micropython-ulab) -- numpy/scipy-like
# numerical computing extension. NOT a core MicroPython module: normally
# integrated via the "user C modules" mechanism (USER_C_MODULES=, a
# code/micropython.mk fragment the top-level Makefile includes), which this
# hand-rolled build doesn't use. Point qstr-scanning directly at our own
# vendored copy (my-overrides/ulab/, committed separately, see "Vendor ulab
# source tree" commit) -- there is no separate "pristine upstream original"
# to point at the way extmod/*.c files above do, since embed.mk has no
# knowledge of ulab at all.
#
# Deliberately staged in two steps: this SRC_QSTR wiring lands first, on its
# own, so `make -f micropython_embed.mk` can be run and genhdr/qstrdefs
# output inspected for ulab-specific qstrs (e.g. Q(ndarray), Q(linspace))
# BEFORE any of these files are added to an actual compile glob. Getting
# this step wrong surfaces as a clean-looking qstr generation pass followed
# by 33 simultaneous "MP_QSTR_* undeclared" compile errors -- much harder to
# debug in one shot than confirming genhdr content first (same class of
# mistake as the extmod/modtime.c SRC_QSTR-without-vendoring bug).
SRC_QSTR += my-overrides/ulab/ndarray.c
SRC_QSTR += my-overrides/ulab/ndarray_operators.c
SRC_QSTR += my-overrides/ulab/ndarray_properties.c
SRC_QSTR += my-overrides/ulab/ulab.c
SRC_QSTR += my-overrides/ulab/ulab_tools.c
SRC_QSTR += my-overrides/ulab/user/user.c
SRC_QSTR += my-overrides/ulab/utils/utils.c
SRC_QSTR += my-overrides/ulab/numpy/numpy.c
SRC_QSTR += my-overrides/ulab/numpy/approx.c
SRC_QSTR += my-overrides/ulab/numpy/bitwise.c
SRC_QSTR += my-overrides/ulab/numpy/compare.c
SRC_QSTR += my-overrides/ulab/numpy/create.c
SRC_QSTR += my-overrides/ulab/numpy/filter.c
SRC_QSTR += my-overrides/ulab/numpy/numerical.c
SRC_QSTR += my-overrides/ulab/numpy/poly.c
SRC_QSTR += my-overrides/ulab/numpy/stats.c
SRC_QSTR += my-overrides/ulab/numpy/transform.c
SRC_QSTR += my-overrides/ulab/numpy/vector.c
SRC_QSTR += my-overrides/ulab/numpy/carray/carray.c
SRC_QSTR += my-overrides/ulab/numpy/carray/carray_tools.c
SRC_QSTR += my-overrides/ulab/numpy/fft/fft.c
SRC_QSTR += my-overrides/ulab/numpy/fft/fft_tools.c
SRC_QSTR += my-overrides/ulab/numpy/io/io.c
SRC_QSTR += my-overrides/ulab/numpy/linalg/linalg.c
SRC_QSTR += my-overrides/ulab/numpy/linalg/linalg_tools.c
SRC_QSTR += my-overrides/ulab/numpy/ndarray/ndarray_iter.c
SRC_QSTR += my-overrides/ulab/numpy/random/random.c
SRC_QSTR += my-overrides/ulab/scipy/scipy.c
SRC_QSTR += my-overrides/ulab/scipy/integrate/integrate.c
SRC_QSTR += my-overrides/ulab/scipy/linalg/linalg.c
SRC_QSTR += my-overrides/ulab/scipy/optimize/optimize.c
SRC_QSTR += my-overrides/ulab/scipy/signal/signal.c
SRC_QSTR += my-overrides/ulab/scipy/special/special.c

# extmod/modtime.c #includes MICROPY_PY_TIME_INCLUDEFILE ("port/
# modtime_android.c", mpconfigport.h) -- OUR OWN file, no upstream
# original, unlike every other SRC_QSTR entry above. The qstr-scan pass
# above (of the pristine $(MICROPYTHON_TOP)/extmod/modtime.c) preprocesses
# with this same CFLAGS, so "port/modtime_android.c" needs to resolve to
# something that exists BEFORE apply-overrides.sh has run (which is what
# actually populates micropython_embed/port/ -- too late for this scan).
# Point CFLAGS at my-overrides/ itself (always present, hand-maintained,
# not generated) so "port/modtime_android.c" resolves to
# my-overrides/port/modtime_android.c during the scan; the real build
# (both native-bringup's own compile step and the real app's CMake build)
# resolves the same "port/modtime_android.c" via -Imicropython_embed once
# apply-overrides.sh has copied it to micropython_embed/port/ -- same
# ordering rule as SRC_QSTR above: must come before `include embed.mk`,
# since CFLAGS is captured at mkrules.mk's parse point.
CFLAGS += -Imy-overrides
# ulab's own files #include each other relative to their code/ root (e.g.
# ulab.c: #include "ndarray.h", "numpy/numpy.h") -- one -I at our vendored
# ulab/ root (my-overrides/ulab/, mirroring their code/ root exactly) is
# enough for every such include across all 33 files, no per-file/per-
# subdirectory -I entries needed. Same before-the-include ordering
# requirement as every other CFLAGS/SRC_QSTR line above.
CFLAGS += -Imy-overrides/ulab

# OpenMV imlib/py_image -- machine vision extension, same "not a core
# MicroPython module" tier as ulab above. Vendored in two stages (see
# SESSION_STATE.yaml, "Vendor OpenMV stub headers"/"Vendor OpenMV
# imlib/common/modules" commits): my-overrides/openmv/ holds both our own
# Camera2-era replacement headers (arm_math.h, framebuffer.h, etc.) AND
# the pristine upstream imlib/common/modules/*.c -- flat, not mirrored
# into subdirectories, confirmed safe before vendoring (every #include
# across the tree is unqualified except two dead-code ones behind
# #if OMV_PROFILER_ENABLE, never defined).
# Only the modules/*.c files (py_image.c etc) actually use MP_QSTR_* --
# the imlib/common files underneath are C-only, no qstr surface of their
# own -- but SRC_QSTR only needs the ones with real usage, same as ulab's
# own SRC_QSTR list above only listing user-facing files, not every
# internal .c ulab vendors.
SRC_QSTR += my-overrides/openmv/py_image.c
SRC_QSTR += my-overrides/openmv/py_helper.c
SRC_QSTR += my-overrides/openmv/py_image_descriptor.c
SRC_QSTR += my-overrides/openmv/py_imageio.c
SRC_QSTR += my-overrides/openmv/py_image_stats.c
# py_clock.c (time.clock() -- OpenMV script compatibility, see
# SESSION_STATE.yaml) -- not imlib/vision code, but same vendored
# location and same "separate translation unit needs its own SRC_QSTR
# entry" reasoning: its tick()/fps()/avg()/reset()/Clock qstrs live in
# this file's own locals_dict_table, not in modtime_android.c (which
# only references &py_clock_type and the exposed `clock` name -- see
# that file's own MICROPY_PY_TIME_EXTRA_GLOBALS entry).
SRC_QSTR += my-overrides/openmv/py_clock.c
# CMSIS_MCU_H: real boards point this at their vendor MCU header (e.g.
# stm32h7xx.h); nothing we compile needs real CMSIS SFR/intrinsic
# definitions once __ARM_ARCH is forced below 7/8 (see arm_math.h) --
# cmsis_mcu_stub.h only needs to exist so the #include resolves. Must be
# on the command line, not a #define inside board_config.h -- imlib.h
# includes fmath.h (which needs it) BEFORE it includes board_config.h,
# so a #define there would always be too late (see board_config.h).
CFLAGS += -DCMSIS_MCU_H='"cmsis_mcu_stub.h"'
# One -I covers both our replacement headers and the vendored imlib/
# common/modules .c/.h files, same flat-directory reasoning as ulab's
# single -Imy-overrides/ulab above.
CFLAGS += -Imy-overrides/openmv
# ulab's own code/ndarray.h, referenced by py_image.c's ulab integration
# via #include "ulab/code/ndarray.h" -- a separate shim root from the
# -Imy-overrides/ulab above, which resolves the flat ndarray.h spelling
# ulab's own files use internally, not this path-qualified one.
CFLAGS += -Imy-overrides/openmv/ulab-shim

# camera_module.cpp -- OUR OWN native csi (camera) module, NOT vendored
# OpenMV code (see the file's own header comment and SESSION_STATE.yaml's
# camera/display native-code-location scoping discussion). Lives directly
# under app/src/main/cpp/, its real permanent location -- NOT under
# my-overrides/ (never copied by apply-overrides.sh) -- so this SRC_QSTR
# entry points straight at the app tree, a real path, unlike the
# throwaway openmv-spike/ scratch location an earlier mistake in this
# same file pointed at (see SESSION_STATE.yaml, corrected before it was
# acted on). Path is relative to native-bringup/, where this Makefile runs.
SRC_QSTR += ../app/src/main/cpp/camera_module.cpp
CFLAGS += -I../app/src/main/cpp
# The qstr-scan pass preprocesses with the HOST gcc (embed.mk's own
# default), not the Android NDK clang wrapper the real build uses --
# unlike every other vendored file so far, camera_module.cpp #includes
# genuine NDK-only headers (<camera/...>, <media/...>). Pointing CFLAGS
# at the real NDK sysroot was tried first and BROKE every other SRC_QSTR
# file in this same combined scan: CFLAGS here is one global list shared
# across the whole `gcc -E file1.c file2.c ...` invocation (this
# Makefile has no per-file CFLAGS mechanism), so the NDK's own libc
# headers (limits.h etc) collided with the host's, e.g. MB_LEN_MAX
# redefined. Fix: empty stub headers (qstr-stub/, just include guards,
# no declarations) that satisfy the #include lines without pulling in
# any real NDK system header -- safe because -E only macro-expands and
# resolves #includes, it never type-checks the ACameraManager* etc.
# bodies that follow, which is all a qstr scan needs.
CFLAGS += -Iqstr-stub

# display_module.cpp -- OUR OWN native display module, same tier/location
# reasoning as camera_module.cpp just above (real app path, qstr-stub
# headers for its own NDK-only #includes -- android/native_window.h,
# reusing the android/log.h stub camera_module.cpp already needed).
SRC_QSTR += ../app/src/main/cpp/display_module.cpp

# imu_module.cpp -- OUR OWN native imu module, same tier/location
# reasoning as camera_module.cpp/display_module.cpp above (real app path,
# NOT vendored OpenMV code -- see SESSION_STATE.yaml's py_imu.c entries).
# Needs two new qstr-stub headers (android/sensor.h, android/looper.h)
# for its own NDK-only #includes -- same empty-include-guard-only shape
# as the existing android/log.h and android/native_window.h stubs.
SRC_QSTR += ../app/src/main/cpp/imu_module.cpp

# Include the main makefile fragment to build the MicroPython component.
include $(MICROPYTHON_TOP)/ports/embed/embed.mk
