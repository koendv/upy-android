// upy-android OpenMV support layer -- OUR OWN imlib_config.h, written
// from scratch (same tier as board_config.h), not copied from any OpenMV
// board. Deliberately minimal: every IMLIB_ENABLE_* feature flag defaults
// to OFF/undefined unless added here after a specific need shows up --
// py_image.c's own method tables are already flag-gated per feature
// (confirmed by reading modules/py_image.c), so an undefined flag cleanly
// compiles out that feature's Python-visible method rather than breaking
// the build.
//
// IMLIB_ENABLE_AGAST (agast.c, GPL-3.0+) is deliberately NOT defined --
// dropped from the plan entirely (see SESSION_STATE.yaml): orb.c below
// does not hard-require it, it can use fast.c instead (BSD-3-Clause,
// and the preferred path in OpenMV's own fallback logic), so the whole
// find_keypoints() feature is reachable with zero GPL code.
#ifndef UPY_ANDROID_IMLIB_CONFIG_H
#define UPY_ANDROID_IMLIB_CONFIG_H

// find_barcodes() -- zbar.c, LGPL-2.1+, self-contained (see
// SESSION_STATE.yaml research entry).
#define IMLIB_ENABLE_BARCODES
// find_qrcodes() -- qrcode.c (quirc), MIT, self-contained, never
// GPL-gated at all -- a separate feature from find_barcodes() above,
// not zbar's own QR support.
#define IMLIB_ENABLE_QRCODES
// find_keypoints() -- orb.c (BSD-3-Clause) + fast.c (BSD-3-Clause, the
// corner detector orb.c actually uses here -- see the AGAST note above).
#define IMLIB_ENABLE_FIND_KEYPOINTS
#define IMLIB_ENABLE_FAST

#endif
