#!/bin/sh
# Reapply this port's overrides on top of a freshly-generated micropython_embed/
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
# modtime_android.c is included textually into extmod/modtime.c via
# MICROPY_PY_TIME_INCLUDEFILE (mpconfigport.h). modtime_android.c must
# live somewhere no glob pattern (py/*.c, port/*.c, shared/runtime/*.c,
# extmod/*.c) will pick up as a separate translation unit, or it gets double-
# compiled (same class of bug hit earlier with extmod/lib/re1.5/*.c).
# micropython_embed/ root is untouched by every existing glob.
cp my-overrides/modtime_android.c micropython_embed/modtime_android.c
mkdir -p micropython_embed/shared/runtime
cp my-overrides/shared-runtime/*.c my-overrides/shared-runtime/*.h micropython_embed/shared/runtime/
# extmod/*.c/.h and extmod/lib/re1.5/ are not copied here. Populated
# already by `make -f micropython_embed.mk`'s own android-extmod-package
# target (embed-android.mk), fetched fresh from $(MICROPYTHON_TOP)
# instead of vendored in my-overrides/.
mkdir -p micropython_embed/shared/timeutils
cp my-overrides/shared-timeutils/timeutils.h micropython_embed/shared/timeutils/timeutils.h
# ulab (numpy/scipy-like numerical extension, not a core MicroPython
# module). 3-level-deep subdirectory tree (e.g. numpy/fft/fft.c), so a
# flat cp like the extmod one above would silently miss files. -r
# preserves the full tree. micropython_embed/ulab/ is untouched by every
# existing CMakeLists.txt glob (py/*.c, port/*.c, shared/runtime/*.c,
# extmod/*.c), same reasoning as modtime_android.c's placement above.
# Merges the pristine upstream tree (vendor/ulab/, fetched fresh, never
# committed, see .gitignore) with ulab_config.h (my-overrides/
# ulab/, the only file in that directory. Everything else there is
# vendor/'s job).
rm -rf micropython_embed/ulab
cp -r vendor/ulab micropython_embed/ulab
cp my-overrides/ulab/ulab_config.h micropython_embed/ulab/ulab_config.h
# OpenMV imlib/py_image, same reasoning and placement as ulab above.
# Flat, not deeply nested like ulab, but still copied wholesale with -r
# for the one nested exception: ulab-shim/ulab/code/ndarray.h (a shim
# path py_image.c's own ulab integration expects). Same vendor/+
# my-overrides/ merge as ulab above: vendor/openmv/ holds the pristine
# upstream files, my-overrides/openmv/ holds this project's own
# replacement headers/glue (board_config.h, arm_math.h, ulab-shim/,
# py_gif.c, etc.), merged flat into one directory, as the build
# expects. py_gif.c is a patched override of a same-named vendor file
# (see its own header comment for why), not a from-scratch replacement
# like board_config.h. The cp -r below intentionally lets py_gif.c win.
# framebuffer.h/.c and queue.h/.c and mutex.h/.c (added for the camera
# decoupling redesign, see session-state) are in the same category as
# board_config.h/arm_math.h -- vendor/openmv/ never had them at all
# (they come from OpenMV's lib/imlib/ and common/ trees, not the
# imlib/py_image scope this project's vendor fetch actually pulls), so
# they're not "patches," just project-owned files placed alongside the
# vendor tree.
rm -rf micropython_embed/openmv
cp -r vendor/openmv micropython_embed/openmv
cp -r my-overrides/openmv/. micropython_embed/openmv/
# AprilTag library (lib/apriltag submodule of OpenMV), used by OpenMV's
# imlib apriltag.c for find_apriltags()/find_rects(). Config header:
# my-overrides/openmv/apriltag_config.h.
rm -rf micropython_embed/apriltag
cp -r vendor/apriltag micropython_embed/apriltag

echo "overrides reapplied"
