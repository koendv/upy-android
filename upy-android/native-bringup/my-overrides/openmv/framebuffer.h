// upy-android OpenMV support layer -- stub replacement for OpenMV's
// lib/imlib/framebuffer.h/.c. Deliberately NOT vendoring the real one:
// it's OpenMV's fixed/pooled hardware-framebuffer + IDE-live-preview
// subsystem (queues, double/triple buffering, fps tracking -- see
// SESSION_STATE.yaml's OpenMV research entry), tied to their sensor
// capture pipeline, which our own Camera2-based architecture won't use.
//
// Real finding from reading the source: this coupling is narrow. No
// imlib/*.c file references framebuffer.h except framebuffer.c itself
// (not vendored here). In py_helper.c/py_image.c it's confined to a
// handful of specific call sites (py_helper_set_to_framebuffer/
// update_framebuffer/is_equal_to_framebuffer -- OpenMV's IDE-streaming
// and sensor-capture glue), none of which our own "load an image (from
// VFS or the camera module), run an op on it" scripts should ever reach.
// The symbols below exist only to satisfy the linker for those call
// sites; see link_stubs.c -- each one aborts loudly if actually called,
// which would mean this assumption was wrong.
#ifndef UPY_ANDROID_STUB_FRAMEBUFFER_H
#define UPY_ANDROID_STUB_FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>
#include "imlib.h"

typedef enum {
    FB_MAINFB_ID = 0,
    FB_STREAM_ID = 1,
    FB_MAX_ID    = 2,
} fb_id_t;

typedef enum {
    FB_FLAG_NONE       = (1 << 0),
    FB_FLAG_USED       = (1 << 1),
    FB_FLAG_FREE       = (1 << 2),
    FB_FLAG_PEEK       = (1 << 3),
    FB_FLAG_CHECK_LAST = (1 << 6),
    FB_FLAG_INVALIDATE = (1 << 7),
} framebuffer_flags_t;

// Trimmed to just the fields our copied call sites actually read (u, v,
// pending, buf_count) -- see SESSION_STATE.yaml for the grep that found
// this exact set. The real struct has many more (queues, mutex, fps
// tracking) that nothing we vendor touches.
typedef struct framebuffer {
    int32_t u, v;
    uint8_t pending;
    size_t buf_count;
} framebuffer_t;

typedef struct vbuffer {
    int32_t offset;
    uint32_t flags;
    uint8_t data[];
} vbuffer_t;

framebuffer_t *framebuffer_get(size_t id);
vbuffer_t *framebuffer_acquire(framebuffer_t *fb, uint32_t flags);
vbuffer_t *framebuffer_release(framebuffer_t *fb, uint32_t flags);
void framebuffer_to_image(framebuffer_t *fb, image_t *img);
void framebuffer_from_image(framebuffer_t *fb, image_t *img);
void framebuffer_update_preview(image_t *src);
int framebuffer_resize(framebuffer_t *fb, size_t count, size_t frame_size);
size_t framebuffer_get_buffer_size(framebuffer_t *fb);

#endif
