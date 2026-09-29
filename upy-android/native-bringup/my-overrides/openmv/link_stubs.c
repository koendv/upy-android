// upy-android OpenMV support layer. Link-satisfying stub bodies for
// omv_csi.h. Each one aborts loudly if actually called. This project's
// own Camera2/NDK-based architecture replaces OpenMV's own sensor-
// capture driver entirely (see omv_csi.h), so nothing in this port
// should ever reach these. Hitting one means that assumption needs
// revisiting, not that the stub needs quiet fallback behavior that
// would hide the surprise.
//
// framebuffer.h/.c's own stubs used to live here too, under the same
// "assumed unreachable" reasoning -- that assumption was wrong (real,
// legitimate callers in py_helper.c/py_image.c reach them) and has been
// reversed: framebuffer.c is now a real, trimmed port (see its own
// header comment), used by camera_module.cpp to decouple continuous
// camera capture from on-demand snapshot() reads. See session-state.
#include <stdio.h>
#include <stdlib.h>
#include "omv_csi.h"

static void omv_unimplemented_stub(const char *fn) {
    fprintf(stderr, "[upy-android openmv] UNIMPLEMENTED stub called: %s "
            "-- csi path was assumed unreachable from our "
            "Camera2-based port, but just got reached\n", fn);
    abort();
}

omv_csi_t *omv_csi_get(int id) {
    (void) id;
    omv_unimplemented_stub("omv_csi_get");
    return NULL;
}

int omv_csi_abort(omv_csi_t *csi, bool fifo_flush, bool in_irq) {
    (void) csi; (void) fifo_flush; (void) in_irq;
    omv_unimplemented_stub("omv_csi_abort");
    return 0;
}
