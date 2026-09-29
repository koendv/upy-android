/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2013-2024 OpenMV, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Framebuffer functions.
 *
 * upy-android: trimmed real port, not a from-scratch replacement -- see
 * session-state: framebuffer.c#UPY_ANDROID_TRIMMED_FRAMEBUFFER_C. Removed:
 * framebuffer_init0() (linker-script memory regions this port doesn't
 * have; camera_module.cpp owns and initializes its own single instance
 * directly). framebuffer_update_preview()'s real body is replaced with a
 * no-op (OpenMV's IDE live-JPEG-streaming-over-USB feature -- this port
 * has no IDE-streaming consumer, but real callers of the surrounding
 * py_helper.c/py_image.c code paths do legitimately reach this function,
 * so it stays present and callable, just inert, rather than aborting).
 * Everything else (framebuffer_init, resize, flush, acquire, release,
 * to_image, from_image, pool_start/end/get) is unmodified from upstream,
 * including uma_malign/uma_free in resize() -- see umalloc_android.c for
 * this port's thin Android shim (plain malloc/free) rather than a
 * bespoke allocator swap here.
 */
#include <stdio.h>
#include <string.h>
#include "framebuffer.h"
#include "umalloc.h"

// Single live instance (FB_MAINFB_ID), reached via framebuffer_get() by
// both camera_module.cpp (the producer) and py_helper.c/py_image.c's own
// default-image code paths -- not OpenMV's own dual mainfb+streamfb
// array (this port has no IDE-streaming consumer for a second buffer).
// camera_module.cpp does NOT keep its own separate framebuffer_t; it
// calls framebuffer_get(FB_MAINFB_ID) to reach this same one, which is
// exactly what makes the py_helper.c/py_image.c integration work.
static framebuffer_t framebuffers[2];

void framebuffer_init(framebuffer_t *fb, void *buff, size_t size, bool dynamic, bool enabled) {
    // Clear framebuffers
    memset(fb, 0, sizeof(framebuffer_t));

    fb->raw_size = size;
    fb->raw_base = buff;
    fb->dynamic = dynamic;
    fb->enabled = enabled;
    #if OMV_RAW_PREVIEW_ENABLE
    fb->raw_w = OMV_RAW_PREVIEW_WIDTH;
    fb->raw_h = OMV_RAW_PREVIEW_HEIGHT;
    fb->raw_enabled = true;
    #else
    fb->raw_w = 0;
    fb->raw_h = 0;
    fb->raw_enabled = false;
    #endif
    // fb->quality left at 0 (memset above) -- only read by the real
    // framebuffer_update_preview(), which this port stubs to a no-op; the
    // upstream OMV_JPEG_QUALITY_* board macros this line used aren't
    // defined here (no IDE-streaming board_config.h in this port).
    mutex_init0(&fb->lock);
}

void framebuffer_to_image(framebuffer_t *fb, image_t *img) {
    if (img != NULL) {
        img->w = fb->w;
        img->h = fb->h;
        img->size = fb->size;
        img->pixfmt = fb->pixfmt;

        // For streaming buffers (no queues), use raw_base directly
        if (fb->used_queue == NULL) {
            img->data = (uint8_t *) fb->raw_base;
        } else {
            vbuffer_t *buffer = framebuffer_acquire(fb, FB_FLAG_USED | FB_FLAG_PEEK);
            img->data = (buffer == NULL) ? NULL : buffer->data;
        }
    }
}

void framebuffer_from_image(framebuffer_t *fb, image_t *img) {
    if (img == NULL) {
        fb->w = 0;
        fb->h = 0;
        fb->size = 0;
        fb->pixfmt = PIXFORMAT_INVALID;
    } else {
        fb->w = img->w;
        fb->h = img->h;
        fb->size = img->size;
        fb->pixfmt = img->pixfmt;
    }
}

framebuffer_t *framebuffer_get(size_t id) {
    // FB_STREAM_ID stays correctly unreachable (NULL): it's OpenMV's own
    // IDE-streaming buffer slot, and nothing in this port has a consumer
    // for it. FB_MAINFB_ID is the one real, live instance -- shared by
    // camera_module.cpp (the producer) and py_helper.c/py_image.c's own
    // default-image code paths, exactly as upstream OpenMV intends.
    if (id != FB_MAINFB_ID) {
        return NULL;
    }
    return &framebuffers[FB_MAINFB_ID];
}

char *framebuffer_pool_start(framebuffer_t *fb, size_t buf_count) {
    size_t qsize = (buf_count <= 3) ? 0 : queue_calc_size(buf_count);
    return fb->raw_base + OMV_ALIGN_TO(qsize * 2, FRAMEBUFFER_ALIGNMENT);
}

char *framebuffer_pool_end(framebuffer_t *fb) {
    char *pool_start = framebuffer_pool_start(fb, fb->buf_count);
    return pool_start + ((fb->buf_size + sizeof(vbuffer_t)) * fb->buf_count);
}

void *framebuffer_pool_get(framebuffer_t *fb, int32_t index) {
    char *pool_start = framebuffer_pool_start(fb, fb->buf_count);
    return pool_start + ((fb->buf_size + sizeof(vbuffer_t)) * index);
}

void framebuffer_flush(framebuffer_t *fb) {
    // Invalidate the frame buffer.
    fb->pixfmt = PIXFORMAT_INVALID;

    // Drop all frame buffers.
    if (fb->buf_count) {
        queue_flush(fb->free_queue);
        queue_flush(fb->used_queue);
    }

    for (size_t i = 0; i < fb->buf_count; i++) {
        vbuffer_t *buffer = framebuffer_pool_get(fb, i);

        // Reset the buffer's state.
        framebuffer_reset(buffer);

        // Discard any cached CPU writes.
        #ifdef __DCACHE_PRESENT
        SCB_InvalidateDCache_by_Addr(buffer->data, fb->buf_size);
        #endif

        // Push it back the free queue.
        queue_push(fb->free_queue, buffer);
    }
}

int framebuffer_resize(framebuffer_t *fb, size_t count, size_t frame_size) {
    // Queue size given the requested buffer count.
    size_t queue_size = queue_calc_size(count);

    // Minimum single buffer size including vbuffer.
    size_t buf_size = OMV_ALIGN_TO(frame_size + sizeof(vbuffer_t), FRAMEBUFFER_ALIGNMENT);

    // Minimum total size including queue overhead.
    size_t min_size = buf_size * count + queue_size * 2;

    if (fb->dynamic) {
        // If a buffer can't grow in place, a new block is allocated first before
        // the old one gets free'd, which could easily fail with big allocations.
        // Free the framebuffer first and use malloc to ensure this doesn't fail.
        if (fb->raw_base) {
            uma_free(fb->raw_base);
            fb->raw_base = NULL;
        }

        void *raw_base = uma_malign(min_size, FRAMEBUFFER_ALIGNMENT, UMA_PERSIST | UMA_MAYBE);
        if (raw_base == NULL) {
            return -1;
        } else {
            fb->raw_size = min_size;
            fb->raw_base = raw_base;
        }
    }

    // Ensure that the raw buffer is large enough.
    if (min_size > fb->raw_size) {
        return -1;
    }

    // Initialize the frame buffer.
    fb->buf_count = count;
    fb->buf_size = buf_size - sizeof(vbuffer_t);

    // Use the frame buffer memory for big queues.
    char *queue_memory = (count > 3) ? fb->raw_base : fb->raw_static;

    // Initialize the buffer queues.
    queue_init(&fb->free_queue, count, &queue_memory[queue_size * 0]);
    queue_init(&fb->used_queue, count, &queue_memory[queue_size * 1]);

    // Flush and reset the queues.
    framebuffer_flush(fb);
    return 0;
}

bool framebuffer_writable(framebuffer_t *fb) {
    return !queue_is_empty(fb->free_queue);
}

bool framebuffer_readable(framebuffer_t *fb) {
    return !queue_is_empty(fb->used_queue);
}

vbuffer_t *framebuffer_acquire(framebuffer_t *fb, uint32_t flags) {
    queue_t *queue = (flags & FB_FLAG_USED) ? fb->used_queue : fb->free_queue;
    vbuffer_t *buffer = queue_pop(queue, (flags & FB_FLAG_PEEK));

    #ifdef __DCACHE_PRESENT
    // Discard any cached CPU writes.
    if (buffer && (flags & FB_FLAG_INVALIDATE)) {
        SCB_InvalidateDCache_by_Addr(buffer->data, fb->buf_size);
    }
    #endif

    return buffer;
}

vbuffer_t *framebuffer_release(framebuffer_t *fb, uint32_t flags) {
    vbuffer_t *buffer = NULL;

    if ((flags & FB_FLAG_CHECK_LAST) && queue_size(fb->free_queue) == 1) {
        if (fb->buf_count == 2) {
            // Double buffer: Reset but do Not release the buffer.
            vbuffer_t *buffer = queue_pop(fb->free_queue, true);
            framebuffer_reset(buffer);
            return NULL;
        } else if (fb->buf_count == 3) {
            // Triple buffer: Swap the old buffer with the latest.
            vbuffer_t *buffer = queue_swap(fb->used_queue, fb->free_queue);
            framebuffer_reset(buffer);
            return NULL;
        }
    }

    if ((buffer = framebuffer_acquire(fb, flags))) {
        if (flags & FB_FLAG_USED) {
            // Invalidate the frame buffer.
            fb->pixfmt = PIXFORMAT_INVALID;
            // Move the buffer back to the free queue.
            framebuffer_reset(buffer);
            queue_push(fb->free_queue, buffer);
        } else {
            // Move the buffer back to the used queue.
            queue_push(fb->used_queue, buffer);
        }
    }

    return buffer;
}

void framebuffer_update_preview(image_t *src) {
    // upy-android: no-op. Real body was OpenMV's IDE live-JPEG-
    // streaming-over-USB feature -- no IDE-streaming consumer exists
    // in this port (framebuffer_get(FB_STREAM_ID) returns NULL above),
    // so this stays present and callable (real callers in py_helper.c/
    // py_image.c legitimately reach it) but inert, not an abort.
    (void) src;
}
