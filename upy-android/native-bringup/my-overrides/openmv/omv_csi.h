// upy-android OpenMV support layer. Stub replacement for OpenMV's
// common/omv_csi.h. See session-state: omv_csi.h#UPY_ANDROID_STUB_OMV_CSI_H
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
