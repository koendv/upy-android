#!/bin/sh
# Reapply our port overrides on top of a freshly-generated micropython_embed/
# bundle. embed.mk always recopies port/*.c fresh from upstream ports/embed/
# port/, so any mpconfigport.h change that affects genhdr (qstrs, moduledefs,
# root pointers) requires: rm -rf build-embed micropython_embed && make -f
# micropython_embed.mk && ./apply-overrides.sh
set -e
cd "$(dirname "$0")"

cp my-overrides/embed_util.c micropython_embed/port/embed_util.c
cp my-overrides/micropython_embed.h micropython_embed/port/micropython_embed.h
cp my-overrides/mphalport.h micropython_embed/port/mphalport.h
cp my-overrides/mphalport.c micropython_embed/port/mphalport.c
# modtime_android.c is included TEXTUALLY into extmod/modtime.c via
# MICROPY_PY_TIME_INCLUDEFILE (mpconfigport.h) -- it must live somewhere
# NO glob pattern (py/*.c, port/*.c, shared/runtime/*.c, extmod/*.c) will
# pick up as a separate translation unit, or it gets double-compiled (same
# class of bug hit earlier with extmod/lib/re1.5/*.c). micropython_embed/
# root is untouched by every existing glob.
cp my-overrides/modtime_android.c micropython_embed/modtime_android.c
mkdir -p micropython_embed/shared/runtime
cp my-overrides/shared-runtime/*.c my-overrides/shared-runtime/*.h micropython_embed/shared/runtime/
# extmod/*.c/.h and extmod/lib/re1.5/ are NOT copied here -- populated
# already by `make -f micropython_embed.mk`'s own android-extmod-package
# target (embed-android.mk), fetched fresh from $(MICROPYTHON_TOP)
# instead of vendored in my-overrides/ (see embed-android.mk's own
# header comment).
mkdir -p micropython_embed/shared/timeutils
cp my-overrides/shared-timeutils/timeutils.h micropython_embed/shared/timeutils/timeutils.h
# ulab (numpy/scipy-like numerical extension, not a core MicroPython
# module) -- 3-level-deep subdirectory tree (e.g. numpy/fft/fft.c), so a
# flat cp like the extmod one above would silently miss files. -r
# preserves the full tree; micropython_embed/ulab/ is untouched by every
# existing CMakeLists.txt glob (py/*.c, port/*.c, shared/runtime/*.c,
# extmod/*.c), same reasoning as modtime_android.c's placement above.
rm -rf micropython_embed/ulab
cp -r my-overrides/ulab micropython_embed/ulab
# OpenMV imlib/py_image -- same reasoning/placement as ulab above. Flat
# (not deeply nested like ulab), but still copied wholesale with -r for
# the one nested exception: ulab-shim/ulab/code/ndarray.h (a shim path
# py_image.c's own ulab integration expects, see SESSION_STATE.yaml).
rm -rf micropython_embed/openmv
cp -r my-overrides/openmv micropython_embed/openmv

echo "overrides reapplied"
