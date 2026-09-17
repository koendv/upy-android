// upy-android OpenMV support layer -- link-satisfying stub bodies for
// framebuffer.h and omv_csi.h. Each one aborts loudly if actually called:
// our Camera2/NDK-based architecture replaces OpenMV's own hardware-
// framebuffer/sensor-capture pipeline entirely (see framebuffer.h/
// omv_csi.h), so nothing in this port should ever reach these -- hitting
// one means that assumption needs revisiting, not that the stub needs
// quiet fallback behavior that would hide the surprise.
//
// Proven correct (not just written) by the compile-only spike
// (native-bringup/openmv-spike/, 2026-09-16): a "load a static image from
// the VFS and run an op on it" test script compiled, linked, and ran
// find_blobs() on-device without ever hitting one of these.
#include <stdio.h>
#include <stdlib.h>
#include "framebuffer.h"
#include "omv_csi.h"

static void omv_unimplemented_stub(const char *fn) {
    fprintf(stderr, "[upy-android openmv] UNIMPLEMENTED stub called: %s "
            "-- framebuffer/csi path was assumed unreachable from our "
            "Camera2-based port, but just got reached\n", fn);
    abort();
}

framebuffer_t *framebuffer_get(size_t id) {
    (void) id;
    omv_unimplemented_stub("framebuffer_get");
    return NULL;
}

vbuffer_t *framebuffer_acquire(framebuffer_t *fb, uint32_t flags) {
    (void) fb; (void) flags;
    omv_unimplemented_stub("framebuffer_acquire");
    return NULL;
}

vbuffer_t *framebuffer_release(framebuffer_t *fb, uint32_t flags) {
    (void) fb; (void) flags;
    omv_unimplemented_stub("framebuffer_release");
    return NULL;
}

void framebuffer_to_image(framebuffer_t *fb, image_t *img) {
    (void) fb; (void) img;
    omv_unimplemented_stub("framebuffer_to_image");
}

void framebuffer_from_image(framebuffer_t *fb, image_t *img) {
    (void) fb; (void) img;
    omv_unimplemented_stub("framebuffer_from_image");
}

void framebuffer_update_preview(image_t *src) {
    (void) src;
    omv_unimplemented_stub("framebuffer_update_preview");
}

int framebuffer_resize(framebuffer_t *fb, size_t count, size_t frame_size) {
    (void) fb; (void) count; (void) frame_size;
    omv_unimplemented_stub("framebuffer_resize");
    return 0;
}

size_t framebuffer_get_buffer_size(framebuffer_t *fb) {
    (void) fb;
    omv_unimplemented_stub("framebuffer_get_buffer_size");
    return 0;
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
