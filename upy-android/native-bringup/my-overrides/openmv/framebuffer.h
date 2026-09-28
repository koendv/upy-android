// upy-android OpenMV support layer. Stub replacement for OpenMV's
// lib/imlib/framebuffer.h/.c. See session-state: framebuffer.h#UPY_ANDROID_STUB_FRAMEBUFFER_H
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

// See session-state: framebuffer.h#UPY_ANDROID_STUB_FRAMEBUFFER_H
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
