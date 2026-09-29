// upy-android OpenMV support layer. Android replacement for OpenMV's
// common/umalloc.c. See session-state: umalloc_android.c#uma_malloc
#include <stdlib.h>
#include "umalloc.h"

void *uma_malloc(size_t size, uint32_t flags) {
    (void) flags;
    return malloc(size);
}

void *uma_calloc(size_t size, uint32_t flags) {
    (void) flags;
    return calloc(1, size);
}

void *uma_realloc(void *ptr, size_t size, uint32_t flags) {
    (void) flags;
    return realloc(ptr, size);
}

// Added for framebuffer_resize() (see session-state: camera framebuffer
// decoupling plan) -- align is ignored, matching every other uma_* shim
// here: glibc/Bionic malloc already returns at least 16-byte-aligned
// blocks, and nothing on Android manually invalidates dcache lines the
// way framebuffer.c's own __DCACHE_PRESENT-gated code would on a Cortex-M
// board (that macro is never defined here), so stricter alignment buys
// nothing on this platform.
void *uma_malign(size_t size, size_t align, uint32_t flags) {
    (void) align; (void) flags;
    return malloc(size);
}

void uma_free(void *ptr) {
    free(ptr);
}

size_t uma_avail(uint32_t flags) {
    (void) flags;
    // No real pool accounting on Android's flat heap. A large-but-
    // finite placeholder so callers that just sanity-check "is there
    // roughly enough room" don't misbehave. Revisit if a real caller
    // needs an honest number.
    return (size_t) 256 * 1024 * 1024;
}
