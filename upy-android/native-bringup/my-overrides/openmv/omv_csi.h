// upy-android OpenMV support layer -- stub replacement for OpenMV's
// common/omv_csi.h.
//
// Real finding (see SESSION_STATE.yaml): the REAL omv_csi.h carries a
// restrictive non-commercial-only license clause (distinct from the
// MIT-licensed imlib/py_image.c we're actually vendoring), AND it's the
// hardware camera-sensor-driver layer our eventual architecture replaces
// with a fresh Camera2 JNI bridge anyway -- so not vendoring it here is
// both a license and an architecture decision, not just a compile
// convenience.
//
// Only used by py_helper.c's framebuffer-preview helpers (same narrow
// call sites as framebuffer.h -- see there), which our own scripts
// shouldn't reach. Just enough surface to link: csi_t's `fb` field
// (py_helper.c does omv_csi_get(-1)->fb) and the two functions called.
#ifndef UPY_ANDROID_STUB_OMV_CSI_H
#define UPY_ANDROID_STUB_OMV_CSI_H

#include <stdbool.h>
#include "framebuffer.h"

typedef struct _omv_csi {
    framebuffer_t *fb;
} omv_csi_t;

omv_csi_t *omv_csi_get(int id);
int omv_csi_abort(omv_csi_t *csi, bool fifo_flush, bool in_irq);

#endif
