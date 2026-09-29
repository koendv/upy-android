// upy-android OpenMV support layer. imlib_config.h, written from
// scratch (same tier as board_config.h), not copied from any OpenMV
// board. Deliberately minimal: every IMLIB_ENABLE_* feature flag
// defaults to off/undefined unless added here after a specific need
// shows up. py_image.c's own method tables are already flag-gated per
// feature, so an undefined flag cleanly compiles out that feature's
// Python-visible method rather than breaking the build.
//
// IMLIB_ENABLE_AGAST (agast.c, GPL-3.0+) is deliberately not defined.
// orb.c below does not hard-require it. It can use fast.c instead
// (BSD-3-Clause, the preferred path in OpenMV's own fallback logic),
// so the whole find_keypoints() feature is reachable with zero GPL code.
#ifndef UPY_ANDROID_IMLIB_CONFIG_H
#define UPY_ANDROID_IMLIB_CONFIG_H

// find_barcodes(): zbar.c, LGPL-2.1+, self-contained.
#define IMLIB_ENABLE_BARCODES
// find_qrcodes(): qrcode.c (quirc), MIT, self-contained, never
// GPL-gated at all. A separate feature from find_barcodes() above,
// not zbar's own QR support.
#define IMLIB_ENABLE_QRCODES
// find_keypoints(): orb.c (BSD-3-Clause) plus fast.c (BSD-3-Clause,
// the corner detector orb.c actually uses here, see the AGAST note above).
#define IMLIB_ENABLE_FIND_KEYPOINTS
#define IMLIB_ENABLE_FAST

// import gif (gif.c/py_gif.c, MIT). file_utils.c (already vendored,
// gated on this same flag) already talks to MicroPython's own VFS, not
// hardware FatFS, so this is a genuinely portable flag flip. Also
// activates a handful of other IMLIB_ENABLE_IMAGE_FILE_IO-gated file
// save/load code paths inside py_image.c/py_image_descriptor.h, not
// exercised or verified beyond gif itself since nothing else currently
// asks for them.
#define IMLIB_ENABLE_IMAGE_FILE_IO

// find_line_segments(): edl.c (LSD, MIT), self-contained, no companion
// flag or dependency.
#define IMLIB_ENABLE_FIND_LINE_SEGMENTS
// find_features(), image.HaarCascade(): haar.c (MIT). Cascades in /rom/, see gen-cascades.sh.
#define IMLIB_ENABLE_FEATURES
// find_circles(): hough.c (MIT), self-contained.
#define IMLIB_ENABLE_FIND_CIRCLES
// find_apriltags(), find_rects(): imlib apriltag.c (MIT) plus the AprilTag
// library (BSD-2-Clause, vendor/apriltag/). Config: apriltag_config.h.
// Families as on OpenMV's RT1060/AE3 boards; HIGH_RES for phone resolutions.
#define IMLIB_ENABLE_FIND_RECTS
#define IMLIB_ENABLE_APRILTAGS
#define IMLIB_ENABLE_APRILTAGS_TAG16H5
#define IMLIB_ENABLE_APRILTAGS_TAG25H9
#define IMLIB_ENABLE_APRILTAGS_TAG36H10
#define IMLIB_ENABLE_APRILTAGS_TAG36H11
#define IMLIB_ENABLE_HIGH_RES_APRILTAGS

#endif
