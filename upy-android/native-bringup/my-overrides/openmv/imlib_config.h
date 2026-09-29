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
// find_lines(): hough.c (MIT), already vendored for find_circles().
#define IMLIB_ENABLE_FIND_LINES
// find_template(): template.c (MIT), already vendored.
#define IMLIB_FIND_TEMPLATE
// find_edges() plus binary(), invert(), and()/or()/xor()/..., erode(),
// dilate(), open(), close(), top_hat(), black_hat(): binary.c and edge.c
// (MIT), already vendored.
#define IMLIB_ENABLE_BINARY_OPS
// add(), sub(), difference(), blend(), min(), max(), negate(), replace(),
// ...: mathop.c (MIT), already vendored. Also needed by top_hat() and
// black_hat() above (imlib_difference_line_op).
#define IMLIB_ENABLE_MATH_OPS
// find_displacement(), logpolar(), linpolar(): phasecorrelation.c (MIT),
// uses the already vendored fft.c.
#define IMLIB_ENABLE_FIND_DISPLACEMENT
#define IMLIB_ENABLE_LOGPOLAR
#define IMLIB_ENABLE_LINPOLAR
// rotation_corr(): rotation_corr.c (MIT), uses the AprilTag library's
// matd/homography. Also needed by find_displacement() with logpolar=True.
#define IMLIB_ENABLE_ROTATION_CORR
// find_lbp(): lbp.c (MIT). image.match_descriptor()/load_descriptor()/
// save_descriptor() for LBP and ORB keypoints: py_image_descriptor.c.
#define IMLIB_ENABLE_FIND_LBP
#define IMLIB_ENABLE_DESCRIPTOR
// find_hog(): hog.c (MIT).
#define IMLIB_ENABLE_HOG
// find_datamatrices(): dmtx.c (libdmtx, BSD-2-Clause), self-contained.
#define IMLIB_ENABLE_DATAMATRICES
// lens_corr(): imlib.c. get_similarity(): stats.c (SSIM).
#define IMLIB_ENABLE_LENS_CORR
#define IMLIB_ENABLE_GET_SIMILARITY
// gaussian(), laplacian(), median(), mean(), mode(), midpoint(),
// bilateral(), morph(): filter.c (MIT), already vendored.
#define IMLIB_ENABLE_GAUSSIAN
#define IMLIB_ENABLE_LAPLACIAN
#define IMLIB_ENABLE_MEDIAN
#define IMLIB_ENABLE_MEAN
#define IMLIB_ENABLE_MODE
#define IMLIB_ENABLE_MIDPOINT
#define IMLIB_ENABLE_BILATERAL
#define IMLIB_ENABLE_MORPH
// flood_fill(): draw.c.
#define IMLIB_ENABLE_FLOOD_FILL
// PNG load/save: png.c plus lodepng.c (zlib license), already vendored.
#define IMLIB_ENABLE_PNG_DECODER
#define IMLIB_ENABLE_PNG_ENCODER
// RGB565 to LAB by table lookup (lab_tab.c, ~96 KB) instead of math.
// Not IMLIB_ENABLE_GAMMA_LUT: only used by the Bayer debayer path.
#define IMLIB_ENABLE_LAB_LUT

#endif
