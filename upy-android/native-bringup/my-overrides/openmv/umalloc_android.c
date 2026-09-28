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
